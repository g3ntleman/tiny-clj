(ns tinyclj.rrd
  (:require [tinyclj.kv :as kv]))

;; Pure Clojure Round-Robin Database implementation for tiny-clj.
;;
;; Inspired by RRDtool/RRD4J semantics:
;; - Fixed step interval (e.g., 300s = 5min)
;; - Multiple RRAs (Round Robin Archives) with different consolidation
;; - Automatic downsampling at update time (no scans needed)
;; - Deterministic O(1) per-sample update cost (cooperative multitasking friendly)
;;
;; Storage: Uses tinyclj.kv for persistence (dogfooding flash-tree).

;; =============================================================================
;; Constants and Helpers
;; =============================================================================

(def rrd-magic 0x52524431)  ;; "RRD1"

;; Consolidation functions
(defn cf-average
  "Compute average (mean) of values, or nil for empty."
  [values]
  (if (empty? values)
    nil
    (/ (reduce + 0.0 values) (count values))))

(defn cf-min
  "Compute minimum of values, or nil for empty."
  [values]
  (if (empty? values)
    nil
    (reduce (fn [a b] (if (< a b) a b)) (first values) (rest values))))

(defn cf-max
  "Compute maximum of values, or nil for empty."
  [values]
  (if (empty? values)
    nil
    (reduce (fn [a b] (if (> a b) a b)) (first values) (rest values))))

(defn cf-last
  "Return last value, or nil for empty."
  [values]
  (if (empty? values)
    nil
    (last values)))

(defn get-cf
  "Resolve a consolidation function keyword to a function."
  [cf-key]
  (case cf-key
    :average cf-average
    :min cf-min
    :max cf-max
    :last cf-last
    (throw (Exception. (str "Unknown CF: " cf-key)))))

;; Helper: map over two vectors by index (tiny-clj map supports only one collection).
(defn map2
  "Map f over two vectors v1/v2 by index."
  [f v1 v2]
  (let [n (count v1)]
    (loop [i 0
           out []]
      (if (>= i n)
        out
        (recur (inc i) (conj out (f (nth v1 i) (nth v2 i))))))))

;; =============================================================================
;; Spline (Piecewise Linear) Helpers
;; =============================================================================

(defn abs
  "Absolute value (numeric)."
  [x]
  (if (< x 0) (- x) x))

(defn spline-fit-line
  "Compute line parameters from running sums.

  Returns {:m slope :b intercept} for y ~= m*x + b."
  [n sx sy sxx sxy]
  (if (< n 2)
    {:m 0.0
     :b (if (= n 0) 0.0 (/ sy n))}
    (let [dn (+ 0.0 n)
          denom (- (* dn sxx) (* sx sx))]
      (if (= denom 0.0)
        {:m 0.0
         :b (/ sy dn)}
        (let [m (/ (- (* dn sxy) (* sx sy)) denom)
              b (/ (- sy (* m sx)) dn)]
          {:m m :b b})))))

(defn spline-segment-predict
  "Predict y at timestamp t for a segment."
  [seg t]
  (let [m (:m seg)
        b (:b seg)
        x (+ 0.0 t)]
    (+ (* m x) b)))

(defn spline-segment-error
  "Return the maximum absolute error currently tracked for the segment."
  [seg]
  (:max-err seg))

(defn spline-segment-create
  "Create a new spline segment initialized with a single point (t, y)."
  [t y]
  (let [x (+ 0.0 t)
        v (+ 0.0 y)]
    {:t0 x
     :t1 x
     :n 1
     :sx x
     :sy v
     :sxx (* x x)
     :sxy (* x v)
     :m 0.0
     :b v
     :max-err 0.0
     :points [[x v]]}))

(defn spline-segment-add
  "Add a point (t, y) to the segment, updating regression and max error."
  [seg t y]
  (let [x (+ 0.0 t)
        v (+ 0.0 y)
        n (inc (:n seg))
        sx (+ (:sx seg) x)
        sy (+ (:sy seg) v)
        sxx (+ (:sxx seg) (* x x))
        sxy (+ (:sxy seg) (* x v))
        fit (spline-fit-line n sx sy sxx sxy)
        m (:m fit)
        b (:b fit)
        points (conj (:points seg) [x v])
        max-err (loop [i 0
                       mx 0.0]
                  (if (>= i (count points))
                    mx
                    (let [p (nth points i)
                          px (nth p 0)
                          py (nth p 1)
                          pred (+ (* m px) b)
                          e (abs (- py pred))]
                      (recur (inc i) (if (> e mx) e mx)))))]
    (-> seg
        (assoc :t1 x)
        (assoc :n n)
        (assoc :sx sx)
        (assoc :sy sy)
        (assoc :sxx sxx)
        (assoc :sxy sxy)
        (assoc :m m)
        (assoc :b b)
        (assoc :max-err max-err)
        (assoc :points points))))

(defn spline-should-split?
  "Return true if adding (t,y) would push the segment beyond epsilon."
  [seg t y epsilon]
  (let [cand (spline-segment-add seg t y)]
    (> (:max-err cand) epsilon)))

(defn spline-finalize-segment
  "Finalize a segment for storage: drop raw points and keep only line params + range."
  [seg]
  {:t0 (:t0 seg)
   :t1 (:t1 seg)
   :m (:m seg)
   :b (:b seg)})

;; =============================================================================
;; Spline-RRA State (Step 3)
;; =============================================================================

(defn make-spline-rra-state
  "Create initial state for a spline RRA."
  [rra-def]
  (let [max-segments (or (:max-segments rra-def) (:rows rra-def))
        epsilon (or (:epsilon rra-def) 0.5)]
    (when (or (nil? max-segments) (<= max-segments 0))
      (throw (Exception. "Spline RRA requires :max-segments (or :rows) > 0")))
    {:type :spline
     :epsilon epsilon
     :max-segments max-segments
     :current-seg nil
     :segments (vec (repeat max-segments nil))
     :ptr 0}))

(defn update-spline-rra
  "Update spline RRA state with a new point (timestamp, value)."
  [spline-state t value]
  (let [eps (:epsilon spline-state)
        cur (:current-seg spline-state)]
    (if (nil? cur)
      (assoc spline-state :current-seg (spline-segment-create t value))
      (if (spline-should-split? cur t value eps)
        (let [final (spline-finalize-segment cur)
              result (push-to-ring (:segments spline-state) (:ptr spline-state) final)
              new-segs (first result)
              new-ptr (second result)]
          (-> spline-state
              (assoc :segments new-segs)
              (assoc :ptr new-ptr)
              (assoc :current-seg (spline-segment-create t value))))
        (assoc spline-state :current-seg (spline-segment-add cur t value))))))

(defn spline-segments-ordered
  "Return all known segments (finalized + current) in chronological order."
  [spline-state]
  (let [ptr (:ptr spline-state)
        segs (:segments spline-state)
        ordered (vec (concat (subvec segs ptr) (subvec segs 0 ptr)))
        finalized (vec (filter some? ordered))
        cur (:current-seg spline-state)]
    (if (nil? cur)
      finalized
      (conj finalized (spline-finalize-segment cur)))))

(defn spline-interpolate
  "Return interpolated value at timestamp t, or nil if no segment covers t."
  [spline-state t]
  (let [x (+ 0.0 t)
        segs (spline-segments-ordered spline-state)]
    (loop [i 0]
      (if (>= i (count segs))
        nil
        (let [seg (nth segs i)
              t0 (:t0 seg)
              t1 (:t1 seg)]
          (if (and (<= t0 x) (<= x t1))
            (+ (* (:m seg) x) (:b seg))
            (recur (inc i))))))))

(defn fetch-spline-rra
  "Fetch view of a spline RRA as regularly spaced samples (like classical RRAs)."
  [rrd rra-index]
  (let [rra-def (nth (:rras rrd) rra-index)
        spline-state (nth (:rra-states rrd) rra-index)
        step (:step rrd)
        eff-step (* step (:steps rra-def))
        rows (:rows rra-def)
        last-update (or (:last-update rrd) 0)
        end-time (normalize-to-step last-update eff-step)
        start-time (- end-time (* eff-step (dec rows)))
        data (loop [i 0
                    out []]
               (if (>= i rows)
                 out
                 (let [t (+ start-time (* eff-step i))
                       v (spline-interpolate spline-state t)]
                   (recur (inc i) (conj out v)))))]
    {:start start-time
     :step eff-step
     :cf (:cf rra-def)
     :data data}))

;; =============================================================================
;; RRD Creation
;; =============================================================================

;; Create initial state for an RRA.
(defn make-rra-state [rra-def]
  (if (= (:cf rra-def) :spline)
    (assoc (make-spline-rra-state rra-def) :cdp-prep {:value nil :count 0})
    {:cdp-prep {:value nil :count 0}
     :data (vec (repeat (:rows rra-def) nil))
     :ptr 0}))

;; Create initial state for an RRD.
(defn make-rrd-state [rrd-def]
  {:last-update nil
   :last-value nil
   :pdp-prep {:value nil :count 0}
   :rra-states (vec (map make-rra-state (:rras rrd-def)))})

;; Create a new RRD definition with initial state.
(defn create [name step rras]
  (when (or (nil? name) (= "" name))
    (throw (Exception. "RRD name must not be empty")))
  (when (or (nil? step) (<= step 0))
    (throw (Exception. "RRD step must be positive")))
  (when (or (nil? rras) (empty? rras))
    (throw (Exception. "RRD must have at least one RRA")))
  (let [rrd-def {:name name
                 :step step
                 :ds {:type :gauge :min nil :max nil}
                 :rras (vec rras)}]
    (merge rrd-def (make-rrd-state rrd-def))))

;; =============================================================================
;; PDP (Primary Data Point) Calculation
;; =============================================================================

;; Normalize a timestamp to the start of its step interval.
(defn normalize-to-step [ts step]
  (* (quot ts step) step))

;; Update PDP preparation with a new value.
(defn update-pdp-prep [pdp-prep value]
  (if (nil? value)
    pdp-prep
    (let [old-val (:value pdp-prep)
          old-cnt (:count pdp-prep)]
      (if (nil? old-val)
        {:value value :count 1}
        {:value (+ old-val value) :count (inc old-cnt)}))))

;; Finalize PDP from preparation.
(defn finalize-pdp [pdp-prep]
  (let [value (:value pdp-prep)
        cnt (:count pdp-prep)]
    (if (or (nil? value) (= 0 cnt))
      nil
      (/ value cnt))))

;; =============================================================================
;; CDP (Consolidated Data Point) Calculation
;; =============================================================================

;; Update CDP preparation with a new PDP value.
(defn update-cdp-prep [cdp-prep pdp-value]
  (if (nil? pdp-value)
    cdp-prep
    (let [old-val (:value cdp-prep)
          old-cnt (:count cdp-prep)]
      (if (nil? old-val)
        {:value (list pdp-value) :count 1}
        {:value (conj (:value cdp-prep) pdp-value) :count (inc old-cnt)}))))

;; Finalize CDP using the consolidation function.
(defn finalize-cdp [cdp-prep cf-key]
  (let [value (:value cdp-prep)
        cnt (:count cdp-prep)]
    (if (or (nil? value) (= 0 cnt))
      nil
      ((get-cf cf-key) value))))

;; Push a value into the ring buffer, advancing the pointer.
(defn push-to-ring [data ptr value]
  (let [new-data (assoc data ptr value)
        new-ptr (mod (inc ptr) (count data))]
    [new-data new-ptr]))

;; Update an RRA state with a new PDP.
(defn update-rra-state
  "Update an RRA state with a new PDP.

  pdp-time is the normalized start-time of the PDP step interval."
  [rra-state rra-def pdp-time pdp-value]
  (let [steps (:steps rra-def)
        cf-key (:cf rra-def)
        cdp-prep (update-cdp-prep (:cdp-prep rra-state) pdp-value)]
    (if (>= (:count cdp-prep) steps)
      ;; Time to consolidate
      (case cf-key
        :spline
        (let [cdp-value (cf-average (:value cdp-prep))
              updated (update-spline-rra rra-state pdp-time cdp-value)]
          (assoc updated :cdp-prep {:value nil :count 0}))

        (let [cdp-value (finalize-cdp cdp-prep cf-key)
              result (push-to-ring (:data rra-state) (:ptr rra-state) cdp-value)
              new-data (first result)
              new-ptr (second result)]
          (-> rra-state
              (assoc :cdp-prep {:value nil :count 0})
              (assoc :data new-data)
              (assoc :ptr new-ptr))))
      ;; Keep accumulating
      (assoc rra-state :cdp-prep cdp-prep))))

;; =============================================================================
;; RRD Update
;; =============================================================================

;; Update an RRD with a new value at the given timestamp.
(defn update-rrd [rrd timestamp value]
  (let [step (:step rrd)
        last-update (:last-update rrd)
        current-slot (normalize-to-step timestamp step)
        last-slot (if last-update (normalize-to-step last-update step) nil)]

    (if (and last-slot (< timestamp last-update))
      ;; Timestamp going backwards - ignore
      rrd

      (if (or (nil? last-slot) (> current-slot last-slot))
        ;; Crossed step boundary: finalize PDP and propagate
        (let [pdp-value (finalize-pdp (:pdp-prep rrd))
              ;; Update all RRA states with the finalized PDP
              new-rra-states (if (nil? pdp-value)
                               (:rra-states rrd)
                               (map2 (fn [rra-state rra-def]
                                       (update-rra-state rra-state rra-def last-slot pdp-value))
                                     (:rra-states rrd)
                                     (:rras rrd)))
              ;; Start new PDP prep with current value
              new-pdp-prep (update-pdp-prep {:value nil :count 0} value)]
          (-> rrd
              (assoc :last-update timestamp)
              (assoc :last-value value)
              (assoc :pdp-prep new-pdp-prep)
              (assoc :rra-states new-rra-states)))

        ;; Same step: just accumulate
        (-> rrd
            (assoc :last-update timestamp)
            (assoc :last-value value)
            (assoc :pdp-prep (update-pdp-prep (:pdp-prep rrd) value)))))))

;; =============================================================================
;; Fetch / Query
;; =============================================================================

;; Calculate the time coverage of an RRA in seconds.
(defn rra-time-coverage [rra-def step]
  (* (:steps rra-def) (:rows rra-def) step))

;; Find the best RRA index for a given time range and CF.
(defn find-best-rra-index [rrd cf-key start-time end-time]
  (let [duration (- end-time start-time)
        step (:step rrd)
        rras (:rras rrd)
        find-match (fn find-match [idx]
                     (if (>= idx (count rras))
                       0  ;; fallback to first
                       (let [rra (nth rras idx)]
                         (if (and (= cf-key (:cf rra))
                                  (>= (rra-time-coverage rra step) duration))
                           idx
                           (find-match (inc idx))))))]
    (find-match 0)))

;; Fetch data from a specific RRA.
(defn fetch-rra [rrd rra-index]
  (let [rra-def (nth (:rras rrd) rra-index)
        cf-key (:cf rra-def)]
    (if (= cf-key :spline)
      (fetch-spline-rra rrd rra-index)
      (let [rra-state (nth (:rra-states rrd) rra-index)
            step (:step rrd)
            eff-step (* step (:steps rra-def))
            rows (:rows rra-def)
            ptr (:ptr rra-state)
            data (:data rra-state)
            ;; Reorder ring buffer: oldest first
            ordered (vec (concat (subvec data ptr) (subvec data 0 ptr)))
            ;; Calculate start time
            last-update (or (:last-update rrd) 0)
            end-time (normalize-to-step last-update eff-step)
            start-time (- end-time (* eff-step (dec rows)))]
        {:start start-time
         :step eff-step
         :cf (:cf rra-def)
         :data ordered}))))

;; Fetch data from the RRD for a time range.
(defn fetch [rrd cf start-time end-time]
  (let [rra-idx (find-best-rra-index rrd cf start-time end-time)]
    (fetch-rra rrd rra-idx)))

;; =============================================================================
;; Persistence (via tinyclj.kv)
;; =============================================================================

;; Generate the KV key for an RRD.
(defn rrd-key [name]
  (str "rrd:" name))

;; Serialize RRD to a byte array (as EDN string bytes).
(defn serialize-rrd [rrd]
  (let [s (pr-str (assoc rrd :magic rrd-magic))]
    (byte-array (map byte s))))

;; Deserialize RRD from a byte array.
(defn deserialize-rrd [bytes]
  (let [s (apply str (map char bytes))
        rrd (read-string s)]
    (when (not= (:magic rrd) rrd-magic)
      (throw (Exception. "Invalid RRD magic")))
    (dissoc rrd :magic)))

;; Persist an RRD to storage.
(defn save! [rrd]
  (let [key (rrd-key (:name rrd))
        bytes (serialize-rrd rrd)]
    (kv/put-bytes key bytes)
    rrd))

;; Load an RRD from storage.
(defn load-rrd [name]
  (let [key (rrd-key name)
        bytes (kv/get-bytes key)]
    (if (nil? bytes)
      nil
      (deserialize-rrd bytes))))

;; Delete an RRD from storage.
(defn delete! [name]
  (kv/delete! (rrd-key name)))

;; =============================================================================
;; Convenience: update and save in one call
;; =============================================================================

;; Update an RRD and persist it.
(defn update! [rrd timestamp value]
  (-> rrd
      (update-rrd timestamp value)
      (save!)))

;; =============================================================================
;; Info / Debug
;; =============================================================================

;; Return info about an RRD structure.
(defn info [rrd]
  {:name (:name rrd)
   :step (:step rrd)
   :last-update (:last-update rrd)
   :rras (map2 (fn [rra-def rra-state]
                 (if (= (:cf rra-def) :spline)
                   {:cf (:cf rra-def)
                    :steps (:steps rra-def)
                    :rows (:rows rra-def)
                    :segments (count (filter some? (:segments rra-state)))
                    :has-current (not (nil? (:current-seg rra-state)))}
                   {:cf (:cf rra-def)
                    :steps (:steps rra-def)
                    :rows (:rows rra-def)
                    :filled (count (filter some? (:data rra-state)))}))
               (:rras rrd)
               (:rra-states rrd))})

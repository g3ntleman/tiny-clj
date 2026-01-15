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
(defn cf-average [values]
  (if (empty? values)
    nil
    (/ (reduce + 0.0 values) (count values))))

(defn cf-min [values]
  (if (empty? values)
    nil
    (reduce (fn [a b] (if (< a b) a b)) (first values) (rest values))))

(defn cf-max [values]
  (if (empty? values)
    nil
    (reduce (fn [a b] (if (> a b) a b)) (first values) (rest values))))

(defn cf-last [values]
  (if (empty? values)
    nil
    (last values)))

(defn get-cf [cf-key]
  (if (= cf-key :average)
    cf-average
    (if (= cf-key :min)
      cf-min
      (if (= cf-key :max)
        cf-max
        (if (= cf-key :last)
          cf-last
          (throw (Exception. (str "Unknown CF: " cf-key))))))))

;; Helper: map over two vectors by index (using recursion, no loop/recur)
(defn map2-helper [f v1 v2 i n result]
  (if (>= i n)
    result
    (map2-helper f v1 v2 (inc i) n (conj result (f (nth v1 i) (nth v2 i))))))

(defn map2 [f v1 v2]
  (map2-helper f v1 v2 0 (count v1) []))

;; =============================================================================
;; RRD Creation
;; =============================================================================

;; Create initial state for an RRA.
(defn make-rra-state [rra-def]
  {:cdp-prep {:value nil :count 0}
   :data (vec (repeat (:rows rra-def) nil))
   :ptr 0})

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
(defn update-rra-state [rra-state rra-def pdp-value]
  (let [steps (:steps rra-def)
        cf-key (:cf rra-def)
        cdp-prep (update-cdp-prep (:cdp-prep rra-state) pdp-value)]
    (if (>= (:count cdp-prep) steps)
      ;; Time to consolidate
      (let [cdp-value (finalize-cdp cdp-prep cf-key)
            result (push-to-ring (:data rra-state) (:ptr rra-state) cdp-value)
            new-data (first result)
            new-ptr (second result)]
        {:cdp-prep {:value nil :count 0}
         :data new-data
         :ptr new-ptr})
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
                                       (update-rra-state rra-state rra-def pdp-value))
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
        rra-state (nth (:rra-states rrd) rra-index)
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
     :data ordered}))

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
                 {:cf (:cf rra-def)
                  :steps (:steps rra-def)
                  :rows (:rows rra-def)
                  :filled (count (filter some? (:data rra-state)))})
               (:rras rrd)
               (:rra-states rrd))})

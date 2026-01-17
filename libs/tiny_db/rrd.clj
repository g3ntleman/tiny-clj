(ns tiny-db.rrd
  (:require [tiny-db.kv :as kv]))

;; Pure Clojure Round-Robin Database implementation for tiny-clj.
;;
;; Inspired by RRDtool/RRD4J semantics:
;; - Fixed step interval (e.g., 300s = 5min)
;; - Multiple RRAs (Round Robin Archives) with different consolidation
;; - Automatic downsampling at update time (no scans needed)
;; - Deterministic O(1) per-sample update cost (cooperative multitasking friendly)
;;
;; Storage: Uses tiny-db.kv for persistence (dogfooding tiny-db).
;;
;; Handler Registration:
;; - RRA types beyond :classic require a handler registered via `register-handler!`
;; - At create time, handler symbol names are stored in the RRD
;; - At load time, handlers are resolved from the global registry

;; =============================================================================
;; Constants and Helpers
;; =============================================================================

(def rrd-magic 0x52524432)  ;; "RRD2" (bumped for handler-types)

;; =============================================================================
;; Global Handler Registry
;; =============================================================================

(def ^:private handler-registry (atom {}))

(defn register-handler!
  "Register an RRA handler under a symbol name.
  
  Called by handler namespaces at load time, e.g.:
    (rrd/register-handler! 'tiny-db.rrd.spline/handler spline-rra-handler)"
  [sym handler-map]
  (swap! handler-registry assoc sym handler-map))

(defn get-handler
  "Look up a handler by its symbol name."
  [sym]
  (get @handler-registry sym))

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
;; RRA Type / Handler Dispatch (pluggable)
;; =============================================================================

(defn rra-type
  "Return the archive type keyword for an RRA definition.

  Backwards compatibility:
  - If :type is missing and :cf is :spline, treat it as :spline."
  [rra-def]
  (or (:type rra-def)
      (if (= (:cf rra-def) :spline) :spline :classic)))

(defn normalize-rra-def
  "Normalize an RRA definition so the core can dispatch consistently."
  [rra-def]
  (let [t (rra-type rra-def)]
    (cond
      (= t :classic)
      (-> rra-def
          (assoc :type :classic)
          (assoc :cdp-cf (or (:cdp-cf rra-def) (:cf rra-def))))

      :else
      (-> rra-def
          (assoc :type t)
          (assoc :cdp-cf (or (:cdp-cf rra-def) :average))))))

(defn require-handler-by-sym
  "Look up handler by symbol, throw if not found."
  [sym]
  (let [h (get-handler sym)]
    (when (nil? h)
      (throw (Exception. (str "No RRA handler registered for: " sym
                              " (did you require the handler namespace?)"))))
    h))

;; =============================================================================
;; RRD Creation
;; =============================================================================

(defn resolve-handler-types
  "Build handler-types map from RRA definitions and opts.
  
  All RRA types must be provided via :handler-types in opts.
  Example: {:handler-types {:classic 'tiny-db.rrd.classic/handler
                            :spline 'tiny-db.rrd.spline/handler}}"
  [rras opts]
  (let [types (set (map :type rras))
        custom (or (:handler-types opts) {})]
    ;; Ensure all types have a symbol
    (loop [ts (seq types)
           m {}]
      (if (nil? ts)
        m
        (let [t (first ts)
              sym (get custom t)]
          (when (nil? sym)
            (throw (Exception. (str "No handler symbol for RRA type: " t
                                    " - provide via :handler-types {" t " 'some.ns/handler}"))))
          (recur (next ts) (assoc m t sym)))))))

(defn resolve-handlers-from-types
  "Resolve handler maps from handler-types symbols via registry."
  [handler-types]
  (loop [ks (keys handler-types)
         m {}]
    (if (nil? ks)
      m
      (let [t (first ks)
            sym (get handler-types t)
            h (require-handler-by-sym sym)]
        (recur (next ks) (assoc m t h))))))

;; Create initial state for an RRA (by type handler).
(defn make-rra-state
  [handlers rra-def]
  (let [t (:type rra-def)
        h (get handlers t)]
    (when (nil? h)
      (throw (Exception. (str "No handler for RRA type: " t))))
    ((:init-state h) rra-def)))

;; Create initial state for an RRD.
(defn make-rrd-state [rrd-def]
  (let [handlers (:handlers rrd-def)]
    {:last-update nil
     :last-value nil
     :pdp-prep {:value nil :count 0}
     :rra-states (vec (map (fn [rra] (make-rra-state handlers rra)) (:rras rrd-def)))}))

;; Create a new RRD definition with initial state.
(defn create
  "Create a new RRD definition with initial state.

  Options:
  - :handler-types {<type> 'symbol.of/handler} for custom RRA types
  
  Example:
    (create \"temp\" 60 [{:cf :average :steps 1 :rows 60}
                         {:type :spline :steps 1 :rows 100 :epsilon 0.1}]
            {:handler-types {:spline 'tiny-db.rrd.spline/handler}})"
  ([name step rras]
   (create name step rras {}))
  ([name step rras opts]
   (when (or (nil? name) (= "" name))
     (throw (Exception. "RRD name must not be empty")))
   (when (or (nil? step) (<= step 0))
     (throw (Exception. "RRD step must be positive")))
   (when (or (nil? rras) (empty? rras))
     (throw (Exception. "RRD must have at least one RRA")))
   (let [norm-rras (vec (map normalize-rra-def rras))
         handler-types (resolve-handler-types norm-rras opts)
         handlers (resolve-handlers-from-types handler-types)
         rrd-def {:name name
                  :step step
                  :ds {:type :gauge :min nil :max nil}
                  :rras norm-rras
                  :handler-types handler-types
                  :handlers handlers}]
     (merge rrd-def (make-rrd-state rrd-def)))))

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
  [handlers rra-state rra-def pdp-time pdp-value]
  (let [steps (:steps rra-def)
        cdp-cf (:cdp-cf rra-def)
        cdp-prep (update-cdp-prep (:cdp-prep rra-state) pdp-value)]
    (if (>= (:count cdp-prep) steps)
      ;; Time to consolidate
      (let [t (:type rra-def)
            h (get handlers t)
            cdp-value (finalize-cdp cdp-prep cdp-cf)
            updated ((:on-cdp h) rra-state rra-def pdp-time cdp-value)]
        (assoc updated :cdp-prep {:value nil :count 0}))
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
        last-slot (if last-update (normalize-to-step last-update step) nil)
        handlers (:handlers rrd)]

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
                                       (update-rra-state handlers rra-state rra-def last-slot pdp-value))
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
                         (let [rk (if (= (:type rra) :classic) (:cf rra) (:type rra))]
                           (if (and (= cf-key rk)
                                  (>= (rra-time-coverage rra step) duration))
                             idx
                             (find-match (inc idx)))))))]
    (find-match 0)))

;; Fetch data from a specific RRA.
(defn fetch-rra [rrd rra-index]
  (let [rra-def (nth (:rras rrd) rra-index)
        rra-state (nth (:rra-states rrd) rra-index)
        t (:type rra-def)
        h (get (:handlers rrd) t)]
    ((:fetch h) rrd rra-index rra-def rra-state)))

;; Fetch data from the RRD for a time range.
(defn fetch [rrd cf start-time end-time]
  (let [rra-idx (find-best-rra-index rrd cf start-time end-time)]
    (fetch-rra rrd rra-idx)))

;; =============================================================================
;; Persistence (via tiny-db.kv)
;; =============================================================================

;; Generate the KV key for an RRD.
(defn rrd-key [name]
  (str "rrd:" name))

;; Serialize RRD to a byte array (as EDN string bytes).
;; Stores :handler-types but not :handlers (runtime-only).
(defn serialize-rrd [rrd]
  (let [rrd2 (dissoc rrd :handlers)
        s (pr-str (assoc rrd2 :magic rrd-magic))]
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
(defn load-rrd
  "Load an RRD from storage.

  Handlers are resolved from the global registry based on stored :handler-types.
  Make sure to require the handler namespaces before loading."
  [name]
  (let [key (rrd-key name)
        bytes (kv/get-bytes key)]
    (if (nil? bytes)
      nil
      (let [rrd0 (deserialize-rrd bytes)
            handler-types (:handler-types rrd0)
            handlers (resolve-handlers-from-types handler-types)]
        (assoc rrd0 :handlers handlers)))))

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
                 (let [t (:type rra-def)
                       h (get (:handlers rrd) t)]
                   ((:info h) rra-def rra-state)))
               (:rras rrd)
               (:rra-states rrd))})

(ns tinyclj.rrd.classic
  (:require [tinyclj.rrd :as rrd]))

;; Classic RRA handler for standard consolidation functions (average, min, max, last).
;; Stores CDPs in a ring buffer.

(def handler
  "Classic RRA handler - register via :handler-types {:classic 'tinyclj.rrd.classic/handler}"
  {:init-state (fn [rra-def]
                 {:cdp-prep {:value nil :count 0}
                  :data (vec (repeat (:rows rra-def) nil))
                  :ptr 0})
   :on-cdp (fn [rra-state rra-def cdp-time cdp-value]
             (let [result (rrd/push-to-ring (:data rra-state) (:ptr rra-state) cdp-value)
                   new-data (first result)
                   new-ptr (second result)]
               (-> rra-state
                   (assoc :data new-data)
                   (assoc :ptr new-ptr))))
   :fetch (fn [rrd-instance rra-index rra-def rra-state]
            (let [step (:step rrd-instance)
                  eff-step (* step (:steps rra-def))
                  rows (:rows rra-def)
                  ptr (:ptr rra-state)
                  data (:data rra-state)
                  ;; Reorder ring buffer: oldest first
                  ordered (vec (concat (subvec data ptr) (subvec data 0 ptr)))
                  ;; Calculate start time
                  last-update (or (:last-update rrd-instance) 0)
                  end-time (rrd/normalize-to-step last-update eff-step)
                  start-time (- end-time (* eff-step (dec rows)))]
              {:start start-time
               :step eff-step
               :cf (:cf rra-def)
               :data ordered}))
   :info (fn [rra-def rra-state]
           {:type :classic
            :cf (:cf rra-def)
            :steps (:steps rra-def)
            :rows (:rows rra-def)
            :filled (count (filter some? (:data rra-state)))})})

;; Register handler at namespace load time.
(rrd/register-handler! 'tinyclj.rrd.classic/handler handler)

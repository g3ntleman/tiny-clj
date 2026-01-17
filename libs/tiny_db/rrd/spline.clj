(ns tiny-db.rrd.spline
  (:require [tiny-db.rrd :as rrd]))

;; =============================================================================
;; Helpers
;; =============================================================================

(defn abs
  "Absolute value (numeric)."
  [x]
  (if (< x 0) (- x) x))

;; =============================================================================
;; Spline Segment Primitives
;; =============================================================================

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
 
 (defn make-spline-rra-state
   "Create initial state for a spline RRA."
   [rra-def]
   (let [max-segments (or (:max-segments rra-def) (:rows rra-def))
         epsilon (or (:epsilon rra-def) 0.5)]
     (when (or (nil? max-segments) (<= max-segments 0))
       (throw (Exception. "Spline RRA requires :max-segments (or :rows) > 0")))
     {:cdp-prep {:value nil :count 0}
      :type :spline
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
               result (rrd/push-to-ring (:segments spline-state) (:ptr spline-state) final)
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
   [rrd-instance rra-def spline-state]
   (let [step (:step rrd-instance)
         eff-step (* step (:steps rra-def))
         rows (:rows rra-def)
         last-update (or (:last-update rrd-instance) 0)
         end-time (rrd/normalize-to-step last-update eff-step)
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
      :cf :spline
      :data data}))
 
(def handler
  "Spline RRA handler - register this via :handler-types {:spline 'tiny-db.rrd.spline/handler}"
  {:init-state (fn [rra-def]
                 (make-spline-rra-state rra-def))
   :on-cdp (fn [rra-state rra-def cdp-time cdp-value]
             (update-spline-rra rra-state cdp-time cdp-value))
   :fetch (fn [rrd-instance rra-index rra-def rra-state]
            (fetch-spline-rra rrd-instance rra-def rra-state))
   :info (fn [rra-def rra-state]
           {:type :spline
            :steps (:steps rra-def)
            :rows (:rows rra-def)
            :epsilon (:epsilon rra-state)
            :segments (count (filter some? (:segments rra-state)))
            :has-current (not (nil? (:current-seg rra-state)))})})

;; Register handler at namespace load time.
(rrd/register-handler! 'tiny-db.rrd.spline/handler handler)
 

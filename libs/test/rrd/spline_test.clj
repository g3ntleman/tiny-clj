;; Spline-RRA tests.
;;
;; Tests spline segment primitives and integration with RRD.

(do
  (load-file "libs/tinyclj/rrd.clj")
  (load-file "libs/tinyclj/rrd/spline.clj")

  (let [abs (fn [x] (if (< x 0) (- x) x))
        assert-eq (fn [expected actual msg]
                    (when (not (= expected actual))
                      (throw (Exception. (str "ASSERT FAIL: " msg " expected=" expected " actual=" actual)))))
        assert-close (fn [expected actual eps msg]
                       (when (> (abs (- expected actual)) eps)
                         (throw (Exception. (str "ASSERT FAIL: " msg " expected=" expected " actual=" actual " eps=" eps)))))
        assert-true (fn [val msg]
                      (when (not val)
                        (throw (Exception. (str "ASSERT FAIL: " msg)))))
        assert-false (fn [val msg]
                       (when val
                         (throw (Exception. (str "ASSERT FAIL: " msg)))))]

    ;; Test 1: Regression on perfect line
    (println "Spline test 1: regression on perfect line")
    ;; y = 2*x + 1
    (let [s0 (tinyclj.rrd.spline/spline-segment-create 0 1)
          s1 (tinyclj.rrd.spline/spline-segment-add s0 1 3)
          s2 (tinyclj.rrd.spline/spline-segment-add s1 2 5)]
      (assert-close 2.0 (:m s2) 1.0e-9 "slope m")
      (assert-close 1.0 (:b s2) 1.0e-9 "intercept b")
      (assert-close 7.0 (tinyclj.rrd.spline/spline-segment-predict s2 3) 1.0e-9 "predict at x=3")
      (assert-close 0.0 (tinyclj.rrd.spline/spline-segment-error s2) 1.0e-9 "max error is zero"))
    (println "  OK")

    ;; Test 2: Error-bounded split decision
    (println "Spline test 2: error-bounded split decision")
    (let [eps 0.01
          s0 (tinyclj.rrd.spline/spline-segment-create 0 0)
          s1 (tinyclj.rrd.spline/spline-segment-add s0 1 1)
          s2 (tinyclj.rrd.spline/spline-segment-add s1 2 2)]
      (assert-false (tinyclj.rrd.spline/spline-should-split? s2 3 3 eps) "no split for on-line point")
      (assert-true (tinyclj.rrd.spline/spline-should-split? s2 3 10 eps) "split for outlier"))
    (println "  OK")

    ;; Test 3: Spline-RRA state management
    (println "Spline test 3: spline-rra state management")
    (let [rra-def {:type :spline :cf :spline :steps 1 :rows 10 :epsilon 0.01 :max-segments 4}
          st0 (tinyclj.rrd.spline/make-spline-rra-state rra-def)
          ;; Add points on line y=x (no split), then outlier to force split.
          st1 (tinyclj.rrd.spline/update-spline-rra st0 0 0)
          st2 (tinyclj.rrd.spline/update-spline-rra st1 1 1)
          st3 (tinyclj.rrd.spline/update-spline-rra st2 2 2)
          st4 (tinyclj.rrd.spline/update-spline-rra st3 3 10)]
      (assert-eq :spline (:type st0) "state has type")
      (assert-true (map? (:current-seg st4)) "current segment exists")
      ;; After split, we should have exactly one finalized segment stored.
      (let [segs (filter some? (:segments st4))]
        (assert-eq 1 (count segs) "one segment finalized after first split")))
    (println "  OK")

    ;; Test 4: Fetch reconstructs samples within epsilon
    (println "Spline test 4: fetch reconstructs samples within epsilon")
    (let [rra-def {:type :spline :cf :spline :steps 1 :rows 6 :epsilon 1.0e-9 :max-segments 8}
          st0 (tinyclj.rrd.spline/make-spline-rra-state rra-def)
          ;; y = 2*t + 1 for t=0..5
          st (reduce (fn [s t] (tinyclj.rrd.spline/update-spline-rra s t (+ 1 (* 2 t)))) st0 (range 6))
          rrd {:step 1
               :last-update 5
               :rras [rra-def]
               :rra-states [st]}
          res (tinyclj.rrd.spline/fetch-spline-rra rrd rra-def st)]
      (assert-eq 0 (:start res) "start time")
      (assert-eq 1 (:step res) "step")
      (assert-eq :spline (:cf res) "cf is spline")
      (let [d (:data res)]
        (assert-eq 6 (count d) "row count")
        (loop [i 0]
          (when (< i 6)
            (assert-close (+ 1 (* 2 i)) (nth d i) 1.0e-6 (str "sample " i))
            (recur (inc i))))))
    (println "  OK")

    ;; Test 5: Integration via create/update-rrd/fetch with handler-types
    (println "Spline test 5: integration via create/update-rrd/fetch")
    (load-file "libs/tinyclj/rrd/classic.clj")
    (let [rrd0 (tinyclj.rrd/create "mix" 1
                 [{:cf :average :steps 1 :rows 6}
                  {:type :spline :steps 1 :rows 6 :epsilon 1.0e-6 :max-segments 8}]
                 {:handler-types {:classic 'tinyclj.rrd.classic/handler
                                  :spline 'tinyclj.rrd.spline/handler}})
          ;; Update 0..7, so PDPs for 0..6 are finalized.
          rrd1 (reduce (fn [r t] (tinyclj.rrd/update-rrd r t (+ 1 (* 2 t)))) rrd0 (range 8))
          avg (tinyclj.rrd/fetch rrd1 :average 0 999)
          spl (tinyclj.rrd/fetch rrd1 :spline 0 999)]
      (assert-eq :average (:cf avg) "average fetch cf")
      (assert-eq :spline (:cf spl) "spline fetch cf")
      (assert-true (vector? (:data avg)) "average data is vector")
      (assert-true (vector? (:data spl)) "spline data is vector")
      ;; Check last few spline samples are close to expected line.
      (let [d (:data spl)]
        (assert-eq 6 (count d) "spline row count")
        (assert-close 3.0 (nth d 1) 1.0e-3 "sample 1 approx")
        (assert-close 11.0 (nth d 5) 1.0e-3 "sample 5 approx")))
    (println "  OK"))

  (println "")
  (println "=== Spline tests: ALL OK ==="))

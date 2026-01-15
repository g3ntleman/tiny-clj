;; Spline-RRA base tests (Step 1 regression primitives).

(do
  (load-file "libs/tinyclj/rrd.clj")

  (let [assert-eq (fn [expected actual msg]
                    (when (not (= expected actual))
                      (throw (Exception. (str "ASSERT FAIL: " msg " expected=" expected " actual=" actual)))))
        assert-close (fn [expected actual eps msg]
                       (when (> (tinyclj.rrd/abs (- expected actual)) eps)
                         (throw (Exception. (str "ASSERT FAIL: " msg " expected=" expected " actual=" actual " eps=" eps)))))]

    (println "Spline test 1: regression on perfect line")
    ;; y = 2*x + 1
    (let [s0 (tinyclj.rrd/spline-segment-create 0 1)
          s1 (tinyclj.rrd/spline-segment-add s0 1 3)
          s2 (tinyclj.rrd/spline-segment-add s1 2 5)]
      (assert-close 2.0 (get s2 :m) 1.0e-9 "slope m")
      (assert-close 1.0 (get s2 :b) 1.0e-9 "intercept b")
      (assert-close 7.0 (tinyclj.rrd/spline-segment-predict s2 3) 1.0e-9 "predict at x=3")
      (assert-close 0.0 (tinyclj.rrd/spline-segment-error s2) 1.0e-9 "max error is zero"))
    (println "  OK"))

  (let [assert-true (fn [val msg]
                      (when (not val)
                        (throw (Exception. (str "ASSERT FAIL: " msg)))))
        assert-false (fn [val msg]
                       (when val
                         (throw (Exception. (str "ASSERT FAIL: " msg)))))]

    (println "Spline test 2: error-bounded split decision")
    (let [eps 0.01
          s0 (tinyclj.rrd/spline-segment-create 0 0)
          s1 (tinyclj.rrd/spline-segment-add s0 1 1)
          s2 (tinyclj.rrd/spline-segment-add s1 2 2)]
      (assert-false (tinyclj.rrd/spline-should-split? s2 3 3 eps) "no split for on-line point")
      (assert-true (tinyclj.rrd/spline-should-split? s2 3 10 eps) "split for outlier"))
    (println "  OK"))

  (let [assert-eq (fn [expected actual msg]
                    (when (not (= expected actual))
                      (throw (Exception. (str "ASSERT FAIL: " msg " expected=" expected " actual=" actual)))))
        assert-true (fn [val msg]
                      (when (not val)
                        (throw (Exception. (str "ASSERT FAIL: " msg)))))]

    (println "Spline test 3: spline-rra state management")
    (let [rra-def {:cf :spline :epsilon 0.01 :max-segments 4}
          st0 (tinyclj.rrd/make-spline-rra-state rra-def)
          ;; Add points on line y=x (no split), then outlier to force split.
          st1 (tinyclj.rrd/update-spline-rra st0 0 0)
          st2 (tinyclj.rrd/update-spline-rra st1 1 1)
          st3 (tinyclj.rrd/update-spline-rra st2 2 2)
          st4 (tinyclj.rrd/update-spline-rra st3 3 10)]
      (assert-eq :spline (get st0 :type) "state has type")
      (assert-true (map? (get st4 :current-seg)) "current segment exists")
      ;; After split, we should have exactly one finalized segment stored.
      (let [segs (filter some? (get st4 :segments))]
        (assert-eq 1 (count segs) "one segment finalized after first split")))
    (println "  OK"))

  (let [assert-close (fn [expected actual eps msg]
                       (when (> (tinyclj.rrd/abs (- expected actual)) eps)
                         (throw (Exception. (str "ASSERT FAIL: " msg " expected=" expected " actual=" actual " eps=" eps)))))
        assert-eq (fn [expected actual msg]
                    (when (not (= expected actual))
                      (throw (Exception. (str "ASSERT FAIL: " msg " expected=" expected " actual=" actual)))))]

    (println "Spline test 4: fetch reconstructs samples within epsilon")
    ;; Build a minimal rrd map and fetch directly from spline RRA.
    (let [rra-def {:cf :spline :steps 1 :rows 6 :epsilon 1.0e-9 :max-segments 8}
          st0 (tinyclj.rrd/make-spline-rra-state rra-def)
          ;; y = 2*t + 1 for t=0..5
          st (reduce (fn [s t] (tinyclj.rrd/update-spline-rra s t (+ 1 (* 2 t)))) st0 (range 6))
          rrd {:step 1
               :last-update 5
               :rras [rra-def]
               :rra-states [st]}
          res (tinyclj.rrd/fetch-spline-rra rrd 0)]
      (assert-eq 0 (get res :start) "start time")
      (assert-eq 1 (get res :step) "step")
      (assert-eq :spline (get res :cf) "cf is spline")
      (let [d (get res :data)]
        (assert-eq 6 (count d) "row count")
        (loop [i 0]
          (when (< i 6)
            (assert-close (+ 1 (* 2 i)) (nth d i) 1.0e-6 (str "sample " i))
            (recur (inc i))))))
    (println "  OK"))

  (let [assert-eq (fn [expected actual msg]
                    (when (not (= expected actual))
                      (throw (Exception. (str "ASSERT FAIL: " msg " expected=" expected " actual=" actual)))))
        assert-true (fn [val msg]
                      (when (not val)
                        (throw (Exception. (str "ASSERT FAIL: " msg)))))
        assert-close (fn [expected actual eps msg]
                       (when (> (tinyclj.rrd/abs (- expected actual)) eps)
                         (throw (Exception. (str "ASSERT FAIL: " msg " expected=" expected " actual=" actual " eps=" eps)))))]

    (println "Spline test 5: integration via create/update-rrd/fetch")
    (let [rrd0 (tinyclj.rrd/create "mix" 1
                 [{:cf :average :steps 1 :rows 6}
                  {:cf :spline :steps 1 :rows 6 :epsilon 1.0e-6 :max-segments 8}])
          ;; Update 0..7, so PDPs for 0..6 are finalized.
          rrd1 (reduce (fn [r t] (tinyclj.rrd/update-rrd r t (+ 1 (* 2 t)))) rrd0 (range 8))
          avg (tinyclj.rrd/fetch rrd1 :average 0 999)
          spl (tinyclj.rrd/fetch rrd1 :spline 0 999)]
      (assert-eq :average (get avg :cf) "average fetch cf")
      (assert-eq :spline (get spl :cf) "spline fetch cf")
      (assert-true (vector? (get avg :data)) "average data is vector")
      (assert-true (vector? (get spl :data)) "spline data is vector")
      ;; Check last few spline samples are close to expected line.
      (let [d (get spl :data)]
        (assert-eq 6 (count d) "spline row count")
        (assert-close 3.0 (nth d 1) 1.0e-3 "sample 1 approx")
        (assert-close 11.0 (nth d 5) 1.0e-3 "sample 5 approx")))
    (println "  OK"))

  (println "")
  (println "=== Spline step1 regression test: ALL OK ==="))


;; RRD Library smoke test.
;;
;; Tests basic create/update/fetch cycle without persistence.

(do
  (load-file "libs/tiny_db/rrd.clj")
  (load-file "libs/tiny-db/rrd-classic.clj")

  (let [assert-eq (fn [expected actual msg]
                    (when (not (= expected actual))
                      (throw (Exception. (str "ASSERT FAIL: " msg " expected=" expected " actual=" actual)))))
        assert-true (fn [val msg]
                      (when (not val)
                        (throw (Exception. (str "ASSERT FAIL: " msg)))))]

    ;; Test 1: Create RRD
    (println "RRD test 1: create")
    (let [rrd (tiny-db.rrd/create "test-temp" 300
                [{:cf :average :steps 1 :rows 12}
                 {:cf :average :steps 6 :rows 24}
                 {:cf :max :steps 1 :rows 12}]
                {:handler-types {:classic 'tiny-db.rrd-classic/handler}})]
      (assert-eq "test-temp" (get rrd :name) "rrd name")
      (assert-eq 300 (get rrd :step) "rrd step")
      (assert-eq 3 (count (get rrd :rras)) "rrd has 3 RRAs")
      (assert-eq nil (get rrd :last-update) "no updates yet")
      (println "  create: OK")

      ;; Test 2: Update with single value
      (println "RRD test 2: single update")
      (let [rrd1 (tiny-db.rrd/update-rrd rrd 1000 25.0)]
        (assert-eq 1000 (get rrd1 :last-update) "last-update set")
        (assert-eq 25.0 (get rrd1 :last-value) "last-value set")
        (println "  single update: OK")

        ;; Test 3: Update crossing step boundary
        (println "RRD test 3: step boundary crossing")
        (let [rrd2 (tiny-db.rrd/update-rrd rrd1 1400 26.0)]
          (assert-eq 1400 (get rrd2 :last-update) "last-update after crossing")
          (println "  step crossing: OK")

          ;; Test 4: Multiple updates
          (println "RRD test 4: multiple updates")
          (let [rrd-final (reduce
                           (fn [r i]
                             (let [ts (+ 0 (* i 300) 150)
                                   val (+ 20.0 (* i 0.5))]
                               (tiny-db.rrd/update-rrd r ts val)))
                           rrd
                           (range 7))]

            (let [rra0-state (first (get rrd-final :rra-states))
                  filled (count (filter some? (get rra0-state :data)))]
              (assert-true (> filled 0) "RRA0 has some data"))
            (println "  multiple updates: OK")

            ;; Test 5: Fetch
            (println "RRD test 5: fetch")
            (let [result (tiny-db.rrd/fetch rrd-final :average 0 10000)]
              (assert-eq :average (get result :cf) "fetch returns correct CF")
              (assert-true (> (get result :step) 0) "fetch has positive step")
              (assert-true (vector? (get result :data)) "fetch returns data vector"))
            (println "  fetch: OK")

            ;; Test 6: Info
            (println "RRD test 6: info")
            (let [info (tiny-db.rrd/info rrd-final)]
              (assert-eq "test-temp" (get info :name) "info has name")
              (assert-eq 3 (count (get info :rras)) "info has 3 RRAs"))
            (println "  info: OK")))))))

  (println "")
  (println "=== RRD smoke test: ALL OK ==="))

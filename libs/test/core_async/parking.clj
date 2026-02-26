;; core.async parking smoke test (macOS/test runtime)
;;
;; Verifies:
;; 1) (go (<! ch)) parks on empty channel and resumes after put!
;; 2) (go expr) schedules evaluation and delivers result

(do
  (require 'clojure.core.async)

  (defn drain-tasks! []
    (if (run-next-task)
      (drain-tasks!)
      nil))

  (let [assert-eq (fn [expected actual msg]
                    (when (not (= expected actual))
                      (throw (str "ASSERT FAIL: " msg " expected=" expected " actual=" actual))))
        in (clojure.core.async/chan)
        parked (clojure.core.async/go (clojure.core.async/<! in))
        immediate (clojure.core.async/go (+ 1 2))]
    (do
      ;; parked take has no value before producer put!
      (assert-eq nil (clojure.core.async/poll! parked) "parked go is initially empty")

      ;; immediate go is asynchronous: result arrives after draining tasks
      (assert-eq nil (clojure.core.async/poll! immediate) "immediate go not yet delivered")
      (drain-tasks!)
      (assert-eq 3 (clojure.core.async/poll! immediate) "immediate go delivers result")

      ;; resume parked go
      (clojure.core.async/put! in 42)
      (drain-tasks!)
      (assert-eq 42 (clojure.core.async/poll! parked) "parked go resumes after put!")
      (println "core_async parking: OK"))))

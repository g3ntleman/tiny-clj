;; macOS-first GPIO simulation smoke test.
;;
;; Requires native clojure.core/gpio-watch + gpio-simulate! and core.async subset.

(do
  (require 'clojure.core.async)
  (require 'tinyclj.gpio)

  (let [eq? (fn [expected actual]
              (if (nil? expected)
                (nil? actual)
                (if (nil? actual) false (= expected actual))))
        assert-eq (fn [expected actual msg]
                    (when (not (eq? expected actual))
                      (throw (str "ASSERT FAIL: " msg " expected=" expected " actual=" actual))))
        drain-events! (fn drain-events! []
                        (if (run-next-task)
                          (drain-events!)
                          nil))]

    (let [m (tinyclj.gpio/gpio-channel 4 (clojure.core.async/sliding-buffer 8))
          ch (get m :ch)
          close! (get m :close!)]
      ;; enqueue two events
      (clojure.core/gpio-simulate! 4 1)
      (clojure.core/gpio-simulate! 4 0)
      (drain-events!)

      (assert-eq (vector 4 1) (clojure.core.async/poll! ch) "first gpio event")
      (assert-eq (vector 4 0) (clojure.core.async/poll! ch) "second gpio event")

      (close!)
      (println "gpio smoke: OK"))))


;; macOS/test helper for tiny-clj.gpio
;;
;; Run:
;;   ./build/tiny-clj-repl -f libs/test/gpio/channel.clj

(do
  (require 'clojure.core.async)
  (require 'tiny-clj.gpio)

  (defn drain-events! []
    (if (run-next-task)
      (drain-events!)
      nil))

  (let [assert-eq (fn [expected actual msg]
                    (when (not (= expected actual))
                      (throw (str "ASSERT FAIL: " msg " expected=" expected " actual=" actual))))
        g (tiny-clj.gpio/gpio-channel 5)
        ch (get g :ch)]
    (do
      (clojure.core/gpio-simulate! 5 1)
      (drain-events!)
      (assert-eq {:source :gpio :kind :edge :pin 5 :value 1}
                 (clojure.core.async/poll! ch)
                 "first gpio event arrives")

      (clojure.core/gpio-simulate! 5 0)
      (drain-events!)
      (assert-eq {:source :gpio :kind :edge :pin 5 :value 0}
                 (clojure.core.async/poll! ch)
                 "second gpio event arrives")

      ((get g :close!))
      (println "gpio channel: OK"))))


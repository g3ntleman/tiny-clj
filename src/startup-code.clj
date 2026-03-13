R"CLOJURE(
(do
  ^#^{:doc "Embedded startup entry point for the ESP32 build."}
  (defn main []
    (let [fx-startup-available?
          (try
            (do
              (require 'tiny-fx.startup)
              true)
            (catch Exception _
              false))]
      (if fx-startup-available?
        (tiny-fx.startup/start!)
        (do
          (println "Hello ESP32!")
          (println "Embedded Clojure running!")))))
  (main))

)CLOJURE"

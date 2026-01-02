R"CLOJURE(
(do
  ^#^{:doc "Embedded startup entry point for the ESP32 demo."}
  (defn main []
    (println "Hello ESP32!")
    (println "Embedded Clojure running!"))
  (main))

)CLOJURE"

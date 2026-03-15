R"CLOJURE(
(do
  ^#^{:doc "Embedded startup entry point for the ESP32 build."}
  (defn -main [& _args]
    (println "Hello ESP32!")
    (println "Embedded Clojure running!")))

)CLOJURE"

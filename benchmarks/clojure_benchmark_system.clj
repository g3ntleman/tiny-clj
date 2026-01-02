;; Benchmark-System komplett in Clojure
;; Verwendet verfügbare Built-ins: println, time-now, dotimes, slurp, spit
;; TODO: time-now muss noch implementiert werden

;; Benchmark-System
^#^{:doc "Führt func iterations-mal aus und liefert eine Ergebnis-Map mit Timing-Daten (in Mikrosekunden)."}
(defn run-benchmark [name func iterations]
  (let [start-time (time-now)
        _ (dotimes [i iterations] (func))
        end-time (time-now)
        duration (- end-time start-time)]
    {:name name
     :iterations iterations
     :duration-micros duration
     :avg-per-iteration (/ duration iterations)}))

^#^{:doc "Gibt ein Benchmark-Ergebnis (Map aus run-benchmark) menschenlesbar auf stdout aus."}
(defn print-benchmark-result [result]
  (println (str "📊 " (:name result) ":"))
  (println (str "   Iterations: " (:iterations result)))
  (println (str "   Total time: " (:duration-micros result) "μs"))
  (println (str "   Avg per iteration: " (:avg-per-iteration result) "μs"))
  (println ""))

^#^{:doc "Formatiert ein einzelnes Benchmark-Ergebnis als eine Textzeile."}
(defn format-benchmark-result [result]
  (str (:name result) ": " (:duration-micros result) "μs (avg: " (:avg-per-iteration result) "μs)\n"))

^#^{:doc "Formatiert eine Sequenz von Benchmark-Ergebnissen zu einem String."}
(defn format-all-results [results]
  (if (empty? results)
    ""
    (let [first-result (format-benchmark-result (first results))
          rest-results (rest results)]
      (if (empty? rest-results)
        first-result
        (str first-result (format-all-results rest-results))))))

^#^{:doc "Speichert Benchmark-Ergebnisse in eine Datei (über spit) und gibt den Zielpfad aus."}
(defn save-benchmark-results [results filename]
  (let [content (str "Benchmark Results:\n"
                     "================\n\n"
                     (format-all-results results))]
    (spit filename content)
    (println (str "✅ Results saved to " filename))))

^#^{:doc "Führt alle Benchmarks aus, druckt Ergebnisse, und speichert sie in benchmark_results.clj."}
(defn run-all-benchmarks []
  (println "🚀 Clojure Benchmark System")
  (println "============================")
  (println "")
  
  (println "📊 Benchmark Results:")
  (println "====================")
  (println "")
  
  ;; Führe Benchmarks einzeln aus
  ;; Hinweis: Diese Funktionen müssen vorher definiert sein:
  ;; - fib
  ;; - test-complex-arithmetic
  ;; - test-function-calls
  ;; - sum-rec
  (let [results [(run-benchmark "fibonacci" #(fib 20) 1000)
                  (run-benchmark "complex-arithmetic" #(test-complex-arithmetic) 1000)
                  (run-benchmark "function-calls" #(test-function-calls) 1000)
                  (run-benchmark "sum-recursive" #(sum-rec 100) 1000)]]
    ;; Drucke Ergebnisse
    (doseq [result results]
      (print-benchmark-result result))
    
    ;; Speichere Ergebnisse in Datei
    (save-benchmark-results results "benchmark_results.clj")
    
    (println "✅ All benchmarks completed!")
    results))

;; Ausführung
(run-all-benchmarks)

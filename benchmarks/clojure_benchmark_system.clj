;; Benchmark-System komplett in Clojure
;; Benötigt File-I/O Built-ins: slurp, spit, file-exists?

;; Beispiel-Implementierung für File-I/O Built-ins:

;; slurp - liest Datei als String
(defn slurp [filename]
  ;; Implementierung als Built-in: native_slurp
  )

;; spit - schreibt String in Datei  
(defn spit [filename content]
  ;; Implementierung als Built-in: native_spit
  )

;; file-exists? - prüft ob Datei existiert
(defn file-exists? [filename]
  ;; Implementierung als Built-in: native_file_exists
  )

;; Benchmark-System
(defn run-benchmark [name func iterations]
  (let [start-time (time-micro)
        _ (dotimes [i iterations] (func))
        end-time (time-micro)
        duration (- end-time start-time)]
    {:name name
     :iterations iterations
     :duration-micros duration
     :avg-per-iteration (/ duration iterations)}))

(defn save-benchmark-results [results filename]
  (let [content (str "Benchmark Results:\n"
                     "================\n"
                     (map #(str (:name %) ": " (:duration-micros %) "μs\n") results))]
    (spit filename content)))

(defn run-all-benchmarks []
  (let [results [(run-benchmark "fibonacci" #(fib 20) 1000)
                 (run-benchmark "arithmetic" #(test-arithmetic) 1000)
                 (run-benchmark "function-calls" #(test-function-calls) 1000)
                 (run-benchmark "sum-recursive" #(sum-rec 100) 1000)]]
    (save-benchmark-results results "benchmark_results.clj")
    results))

;; Ausführung
(run-all-benchmarks)

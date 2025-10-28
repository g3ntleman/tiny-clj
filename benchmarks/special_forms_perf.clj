;; Special Forms Performance Benchmark Suite
;; Tests performance of def, defn, ns before and after conversion to builtins

;; Test 1: def performance (1000 iterations)
(defn test-def-performance []
  (println "Testing def performance...")
  (time
    (dotimes [i 1000]
      (eval (read-string (str "(def test-var-" i " " i ")"))))))

;; Test 2: defn performance (1000 iterations)  
(defn test-defn-performance []
  (println "Testing defn performance...")
  (time
    (dotimes [i 1000]
      (eval (read-string (str "(defn test-fn-" i " [x] (+ x " i "))"))))))

;; Test 3: ns performance (100 iterations)
(defn test-ns-performance []
  (println "Testing ns performance...")
  (time
    (dotimes [i 100]
      (eval (read-string (str "(ns user.test" i ")"))))))

;; Test 4: Combined workload test
(defn test-combined-workload []
  (println "Testing combined workload...")
  (time
    (do
      ;; Mix of def and defn calls
      (dotimes [i 500]
        (eval (read-string (str "(def var-" i " " i ")"))))
      (dotimes [i 500]
        (eval (read-string (str "(defn fn-" i " [x] (* x " i "))"))))
      ;; Some ns calls
      (dotimes [i 50]
        (eval (read-string (str "(ns user.perf" i ")")))))))

;; Run all benchmarks
(defn run-all-benchmarks []
  (println "=== Special Forms Performance Benchmarks ===")
  (println "Baseline measurements (before conversion to builtins)")
  (println)
  
  (test-def-performance)
  (println)
  
  (test-defn-performance)
  (println)
  
  (test-ns-performance)
  (println)
  
  (test-combined-workload)
  (println)
  
  (println "=== Benchmark completed ==="))

;; Run benchmarks
(run-all-benchmarks)

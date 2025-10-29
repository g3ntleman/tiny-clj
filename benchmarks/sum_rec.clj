;; Simple recursive sum
(defn sum-rec [n]
  (if (<= n 0)
    0
    (+ n (sum-rec (- n 1)))))

;; Run 1000x more iterations for measurable timing
(defn benchmark-sum-rec []
  (println "Running sum-rec benchmark (1000x iterations)...")
  (time
    (dotimes [i 1000]
      (sum-rec 100))))

(benchmark-sum-rec)

;; Sleep Benchmark to test time function
;; This should show measurable timing

(defn benchmark-sleep []
  (println "Running sleep benchmark...")
  (time
    (do
      (sleep 1)
      (sleep 1)
      (sleep 1))))

(benchmark-sleep)

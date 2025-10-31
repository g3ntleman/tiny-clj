;; Benchmark für Environment-Chaining Performance
;; Testet verschachtelte let-Blöcke, die vorher Environment-Kopien erzeugten

(defn test-nested-let []
  (let [a 1]
    (let [b (+ a 1)]
      (let [c (+ b 1)]
        (let [d (+ c 1)]
          (let [e (+ d 1)]
            (+ a b c d e)))))))

(defn test-many-nested-lets []
  (let [x 0]
    (let [y (+ x 1)]
      (let [z (+ y 1)]
        (let [w (+ z 1)]
          (let [v (+ w 1)]
            (let [u (+ v 1)]
              (let [t (+ u 1)]
                (let [s (+ t 1)]
                  (let [r (+ s 1)]
                    (+ x y z w v u t s r))))))))))

(defn benchmark-let-chaining []
  (println "=== Let Environment-Chaining Benchmark ===")
  (println "Testing nested let blocks (should benefit from environment chaining)...")
  (time
    (dotimes [i 10000]
      (test-nested-let)
      (test-many-nested-lets)))
  (println "=== Benchmark completed ==="))

(benchmark-let-chaining)


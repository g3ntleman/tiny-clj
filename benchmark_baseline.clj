;; Baseline Benchmark für Symbol Dispatch Performance
;; Testet die aktuelle Performance mit sym_is() String-Vergleichen

;; Einfacher Rekursionstest
(def factorial (fn [n acc] 
  (if (= n 0) 
    acc 
    (recur (- n 1) (* n acc)))))

;; Arithmetische Operationen (häufige sym_is() Aufrufe)
(def arithmetic-test (fn [n] 
  (if (= n 0) 
    0 
    (+ (* n n) (arithmetic-test (- n 1))))))

;; Mixed operations (viele verschiedene Symbole)
(def mixed-ops (fn [n] 
  (if (= n 0) 
    :done 
    (do 
      (+ n 1)
      (- n 1) 
      (* n 2)
      (/ n 2)
      (= n 1)
      (if true n false)
      (mixed-ops (- n 1))))))

;; Teste Performance
(println "Baseline Benchmark Start...")
(println "Factorial 5:" (factorial 5 1))
(println "Arithmetic 10:" (arithmetic-test 10))
(println "Mixed ops 5:" (mixed-ops 5))
(println "Baseline Benchmark Complete")

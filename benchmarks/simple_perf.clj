;; Simple Special Forms Performance Benchmark
;; Tests performance of def, defn, ns before conversion to builtins

;; Test 1: def performance
(println "Testing def performance...")
(time (def test-var-1 42))
(time (def test-var-2 43))
(time (def test-var-3 44))
(time (def test-var-4 45))
(time (def test-var-5 46))

;; Test 2: defn performance  
(println "Testing defn performance...")
(time (defn test-fn-1 [x] (+ x 1)))
(time (defn test-fn-2 [x] (+ x 2)))
(time (defn test-fn-3 [x] (+ x 3)))
(time (defn test-fn-4 [x] (+ x 4)))
(time (defn test-fn-5 [x] (+ x 5)))

;; Test 3: ns performance
(println "Testing ns performance...")
(time (ns user.test1))
(time (ns user.test2))
(time (ns user.test3))

(println "Baseline measurements completed")

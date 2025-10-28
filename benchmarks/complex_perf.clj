;; Complex Performance Tests for Special Forms vs Builtins
;; Tests more complex expressions where performance differences should be visible

;; Test 1: Nested def operations (should show Builtin advantage)
(defn test-nested-defs []
  (let [iterations 1000]
    (time
      (dotimes [i iterations]
        (def (symbol (str "test-var-" i)) i)))))

;; Test 2: Multiple namespace switches (should show Builtin advantage)
(defn test-namespace-switches []
  (let [iterations 100]
    (time
      (dotimes [i iterations]
        (ns (symbol (str "test-ns-" i)))
        (def test-var i)))))

;; Test 3: Complex arithmetic with multiple operations
(defn test-complex-arithmetic []
  (let [iterations 10000]
    (time
      (dotimes [i iterations]
        (+ (* i 2) (- i 1) (/ i 2))))))

;; Test 4: Function calls with multiple arguments
(defn test-function-calls []
  (let [iterations 5000]
    (time
      (dotimes [i iterations]
        (+ i i i i i)))))

;; Test 5: Nested function calls
(defn test-nested-calls []
  (let [iterations 2000]
    (time
      (dotimes [i iterations]
        (+ (* i 2) (+ i 1) (- i 1))))))

;; Run all tests
(println "=== Complex Performance Tests ===")
(println "Test 1: Nested def operations")
(test-nested-defs)

(println "Test 2: Namespace switches")
(test-namespace-switches)

(println "Test 3: Complex arithmetic")
(test-complex-arithmetic)

(println "Test 4: Function calls")
(test-function-calls)

(println "Test 5: Nested calls")
(test-nested-calls)

(println "=== Tests completed ===")

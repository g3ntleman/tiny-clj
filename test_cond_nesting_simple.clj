;; Very simple test to demonstrate the cond nesting problem
;; This test shows that cond receives a nested structure

;; Expected structure: (cond true 1 false 2)
;;   -> list_rest_normalized should return: (true 1 false 2)
;;
;; Actual structure (from debug output): (cond [List: (true 1 false 2)])
;;   -> list_rest_normalized returns: [List: (true 1 false 2)]
;;
;; The problem: The tests and expressions are wrapped in an extra list

(println "=== Simple cond nesting test ===")
(println "Testing: (cond true 1 false 2)")

;; This works, but the internal structure is wrong
(let [result (cond true 1 false 2)]
  (println "Result:" result)
  (println "Expected: 1")
  (if (= result 1)
    (println "✓ Works, but structure is nested internally")))

;; Test with :else - this is where the problem shows up
(println "\nTesting: (cond false 1 :else 2)")
(let [result (cond false 1 :else 2)]
  (println "Result:" result)
  (println "Expected: 2")
  (if (= result 2)
    (println "✓ Works, but structure is nested internally")))

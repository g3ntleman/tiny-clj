;; Simple test to demonstrate the cond nesting problem
;; Expected: (cond true 1 false 2) should return 1
;; Problem: cond receives nested structure instead of flat list

(println "Testing simple cond form:")
(println "(cond true 1 false 2)")

;; This should work and return 1
(let [result (cond true 1 false 2)]
  (println "Result:" result)
  (if (= result 1)
    (println "PASS: cond works correctly")
    (println "FAIL: cond returned" result "instead of 1")))

;; Test with :else
(let [result (cond false 1 :else 2)]
  (println "Testing (cond false 1 :else 2):")
  (println "Result:" result)
  (if (= result 2)
    (println "PASS: cond :else works correctly")
    (println "FAIL: cond :else returned" result "instead of 2")))

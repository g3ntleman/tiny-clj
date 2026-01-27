;; Minimal test to demonstrate the cond nesting problem
;; This test shows the exact error that occurs

(println "=== Minimal cond nesting test ===")
(println "")
(println "Test 1: Direct cond call (works)")
(println "  (cond true 1 false 2)")
(cond true 1 false 2)

(println "")
(println "Test 2: Cond during macro expansion (fails)")
(println "  (normalize-for-bindings '[x [1 2 3]])")
(println "  Expected: [x [1 2 3]]")
(println "  Actual: IllegalArgumentException: cond requires an even number of forms")
(normalize-for-bindings '[x [1 2 3]])

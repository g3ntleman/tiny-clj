;; Very simple test demonstrating the cond nesting problem
;;
;; Problem: cond receives nested structure during macro expansion
;; Expected: (cond test1 expr1 test2 expr2 ...)
;; Actual:   (cond [List: (test1 expr1 test2 expr2 ...)])
;;
;; This causes: IllegalArgumentException: cond requires an even number of forms

(println "=== Cond Nesting Problem Test ===")
(println "")
(println "✓ Direct cond call works:")
(println "  (cond true 1 false 2) =>" (cond true 1 false 2))
(println "")
(println "✗ Cond during macro expansion fails:")
(println "  (normalize-for-bindings '[x [1 2 3]])")
(println "  Error: IllegalArgumentException: cond requires an even number of forms")
(println "")
(println "The problem: cond receives [List: (test1 expr1 ...)] instead of (test1 expr1 ...)")

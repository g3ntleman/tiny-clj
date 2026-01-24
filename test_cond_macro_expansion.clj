;; Test to demonstrate the cond nesting problem during macro expansion
;; The problem occurs when cond is evaluated during macro expansion,
;; not when cond is called directly

(println "=== Testing cond during macro expansion ===")

;; This should work - direct cond call
(println "1. Direct cond call:")
(let [result (cond true 1 false 2)]
  (println "   Result:" result "✓"))

;; This fails - cond during macro expansion (in normalize-for-bindings)
(println "\n2. Cond during macro expansion (normalize-for-bindings):")
(println "   This should return [x [1 2 3]] but fails with:")
(println "   IllegalArgumentException: cond requires an even number of forms")
(let [result (normalize-for-bindings '[x [1 2 3]])]
  (println "   Result:" result "✓"))

;; The problem: normalize-for-bindings uses cond internally,
;; and when cond is evaluated during macro expansion, it receives
;; a nested structure: (cond [List: (test1 expr1 ...)]) instead of
;; (cond test1 expr1 ...)

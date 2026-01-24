;; Test script to compare macro expansion results
;; Expected output from Clojure/JVM:

;; (macroexpand-1 '(for [x [1 2 3]] x))
;; => (for* [x [1 2 3]] x)

;; (normalize-for-bindings '[x [1 2 3]])
;; => [x [1 2 3]]

;; The cond form in normalize-for-bindings should be:
;; (cond
;;   (= item :when) ...
;;   (= item :while) ...
;;   (= item :let) ...
;;   :else ...)
;;
;; Which should be a flat list: (cond test1 expr1 test2 expr2 ... :else expr)

(println "Expected macroexpand-1 result:")
(println "(for* [x [1 2 3]] x)")

(println "\nExpected normalize-for-bindings result:")
(println "[x [1 2 3]]")

(println "\nThe cond form structure should be flat:")
(println "(cond (= item :when) ... (= item :while) ... (= item :let) ... :else ...)")

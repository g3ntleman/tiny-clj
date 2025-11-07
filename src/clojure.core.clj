
R"CLOJURE(
; ============================================================================
; Tiny-CLJ Core Functions
; ============================================================================

; ============================================================================
; Arithmetic Functions
; ============================================================================
(def add (fn [a b] (+ a b)))
(def sub (fn [a b] (- a b)))
(def mul (fn [a b] (* a b)))
(def div (fn [a b] (/ a b)))
(def inc (fn [x] (+ x 1)))
(def dec (fn [x] (- x 1)))
(def square (fn [x] (* x x)))

; ============================================================================
; Numeric Predicates
; ============================================================================
(def zero? (fn [x] (= x 0)))
(def pos? (fn [x] (> x 0)))
(def neg? (fn [x] (< x 0)))
(def even? (fn [x] (= (mod x 2) 0)))
(def odd? (fn [x] (not (= (mod x 2) 0))))

; ============================================================================
; Comparison & Logic
; ============================================================================
(def not (fn [x] (if x false true)))
(def max (fn [a b] (if (> a b) a b)))
(def min (fn [a b] (if (< a b) a b)))

; ============================================================================
; Collection Functions
; ============================================================================
(def second (fn [coll] (first (rest coll))))
(def empty? (fn [coll] (= (count coll) 0)))
(def update (fn [map key f]
  (assoc map key (f (get map key)))))

; ============================================================================
; Utility Functions
; ============================================================================
(def identity (fn [x] x))

; ============================================================================
; Higher-Order Functions
; ============================================================================
(def map (fn [f coll]
  (if (empty? coll)
    (list)
    (cons (f (first coll)) (recur f (rest coll))))))

(def filter (fn [pred coll]
  (let [step (fn [pred coll acc]
                (if (empty? coll)
                  (reverse acc)
                  (if (pred (first coll))
                    (recur pred (rest coll) (cons (first coll) acc))
                    (recur pred (rest coll) acc))))]
    (step pred coll (list)))))

; ============================================================================
; Utility Functions
; ============================================================================
(def constantly (fn [x] (fn [y] x)))
)CLOJURE"

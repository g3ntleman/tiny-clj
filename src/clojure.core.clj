
R"CLOJURE(

(ns clojure.core)

;; Core macros (interpreted as functions for Tiny-Clj)
(def defn (fn [name args & body]
  (def name (fn args body))))

(def fn (fn [& args]
  (fn args)))

;; Arithmetic
(def inc (fn [x] (+ x 1)))
(def dec (fn [x] (- x 1)))
(def add (fn [a b] (+ a b)))
(def sub (fn [a b] (- a b)))
(def mul (fn [a b] (* a b)))
(def div (fn [a b] (/ a b)))
(def square (fn [x] (* x x)))

;; Map utilities
(def map-get (fn [m k] (get m k)))

;; Conditional
(def when (fn [pred & body]
  (if pred (do body))))

;; Function helpers
(def identity (fn [x] x))
(def constantly (fn [v] (fn [& _] v)))

;; Basic predicates
(def nil? (fn [x] (= x nil)))
(def true? (fn [x] (= x true)))
(def false? (fn [x] (= x false)))

;; Sequence functions - Native implementations available
;; (def seq (fn [coll] (seq coll)))           ; ← Native implementation
;; (def first (fn [coll] (first coll)))       ; ← Native implementation  
;; (def rest (fn [coll] (rest coll)))         ; ← Native implementation
;; (def next (fn [coll] (rest coll)))         ; ← Native implementation
;; (def empty? (fn [coll] (= (count coll) 0))) ; ← Native implementation
;; (def count (fn [coll] (count coll)))       ; ← Native implementation

;; For-loop functions - Native C implementations
;; (def for eval_for)           ; ← Native C implementation - REMOVED: causes infinite recursion
;; (def doseq eval_doseq)       ; ← Native C implementation - REMOVED: causes infinite recursion
;; (def dotimes eval_dotimes)   ; ← Native C implementation - REMOVED: causes infinite recursion

;; Printing
(def println (fn [& args]
  (apply println args)))

;; more core functions can be added here...

)CLOJURE"

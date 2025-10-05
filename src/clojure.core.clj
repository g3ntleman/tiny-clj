
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

;; Sequence functions
(def seq (fn [coll] (seq coll)))
(def first (fn [coll] (first coll)))
(def rest (fn [coll] (rest coll)))
(def next (fn [coll] (rest coll)))
(def empty? (fn [coll] (= (count coll) 0)))
(def count (fn [coll] (count coll)))

;; For-loop functions
(def for (fn [binding expr] (for binding expr)))
(def doseq (fn [binding expr] (doseq binding expr)))
(def dotimes (fn [binding expr] (dotimes binding expr)))

;; Printing
(def println (fn [& args]
  (apply println args)))

;; more core functions can be added here...

)CLOJURE"

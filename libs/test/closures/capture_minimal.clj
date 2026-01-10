;; Minimal closure capture fixtures for tiny-clj unity tests.
;; Loaded via (require 'test.closures.capture-minimal).

(ns test.closures.capture-minimal)

;; Capture a function parameter after the outer function returns.
(defn make-adder [x]
  (fn [y] (+ x y)))

;; Capture a let-local after the outer function returns.
(defn make-adder-let [x]
  (let [k x]
    (fn [y] (+ k y))))

;; Nested closures capturing multiple lexical values.
(defn make-nested [a]
  (fn [b]
    (fn [c]
      (+ a b c))))

;; Capture inside a loop (captures the final value).
;; Note: Uses explicit recursion (no loop/recur assumptions).
(defn make-loop-capturer [n]
  (let [final
        ((fn step [i]
           (if (= i 0)
             n
             (step (- i 1))))
         n)]
    (fn [] final)))

;; Dynamic vars must be resolved at call-time, not captured.
(def ^:dynamic *dyn* 1)
(defn make-dyn-reader []
  (fn [] *dyn*))



R"CLOJURE(
(ns clojure.core)

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
(def empty? (fn [coll] 
  (if coll
    (= (count coll) 0)
    true)))
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
    (cons (f (first coll)) (map f (rest coll))))))

(def filter (fn [pred coll]
  (let [step (fn [pred coll acc]
                (if (empty? coll)
                  (if (empty? acc)
                    nil
                    (reverse acc))
                  (if (pred (first coll))
                    (step pred (rest coll) (cons (first coll) acc))
                    (step pred (rest coll) acc))))]
    (step pred coll (list)))))

; ============================================================================
; Utility Functions
; ============================================================================
(def constantly (fn [x] (fn [y] x)))

; ============================================================================
; Metadata Functions
; ============================================================================
^#^{:doc "Returns the metadata of obj, returns nil if there is no metadata."}
(defn meta [x] :native)

; ============================================================================
; Reduce Functions
; ============================================================================
^#^{:doc "f should be a function of 2 arguments. Returns the result of applying f to val and the first item in coll, then applying f to that result and the 2nd item, etc. If coll contains no items, returns val and f is not called."}
(defn reduce [f coll] :native)

; ============================================================================
; Arithmetic Functions (Native)
; ============================================================================
(defn + [& args] :native)
(defn - [& args] :native)
(defn * [& args] :native)
(defn / [& args] :native)
(defn mod [num div] :native)
(defn quot [num div] :native)
(defn bit-shift-left [x n] :native)

; ============================================================================
; Sequence Functions (Native)
; ============================================================================
(defn range [& args] :native)
(defn repeat [x] :native)

; ============================================================================
; Math Functions (Native)
; ============================================================================
(defn sqrt [x] :native)

; ============================================================================
; String Functions (Native)
; ============================================================================
(defn str [& args] :native)
(defn subs [s start & end] :native)

; ============================================================================
; Symbol Functions (Native)
; ============================================================================
(defn symbol [& args] :native)

; ============================================================================
; Type Functions (Native)
; ============================================================================
(defn type [x] :native)

; ============================================================================
; Collection Functions (Native)
; ============================================================================
(defn array-map [& keyvals] :native)
(defn vector [& args] :native)
(defn vec [coll] :native)
(defn nth [coll index & not-found] :native)
(defn peek [coll] :native)
(defn pop [coll] :native)
(defn subvec [v start & end] :native)
(defn conj [coll & items] :native)
(defn first [coll] :native)
(defn rest [coll] :native)
(defn next [coll] :native)
(defn cons [x seq] :native)
(defn list [& items] :native)
(defn count [coll] :native)
(defn reverse [coll] :native)
(defn assoc [map key val & kvs] :native)
(defn dissoc [map & keys] :native)
(defn transient [coll] :native)
(defn persistent! [tcoll] :native)
(defn conj! [tcoll val] :native)
(defn get [map key & not-found] :native)
(defn keys [map] :native)
(defn vals [map] :native)

; ============================================================================
; Predicate Functions (Native)
; ============================================================================
(defn nil? [x] :native)
(defn vector? [x] :native)
(defn map? [x] :native)

; ============================================================================
; Comparison Functions (Native)
; ============================================================================
(defn < [x y] :native)
(defn > [x y] :native)
(defn <= [x y] :native)
(defn >= [x y] :native)
(defn = [x y] :native)
(defn not= [x y] :native)
(defn identical? [x y] :native)

; ============================================================================
; Print Functions (Native)
; ============================================================================
(defn println [& args] :native)
(defn print [& args] :native)
(defn pr [& args] :native)
(defn prn [& args] :native)

; ============================================================================
; Format Functions (Native)
; ============================================================================
(defn format [fmt & args] :native)

; ============================================================================
; Control Flow Functions (Native)
; ============================================================================
(defn do [& exprs] :native)

; ============================================================================
; Namespace Functions (Native)
; ============================================================================
(defn ns-map [ns] :native)
(defn find-ns [sym] :native)

; ============================================================================
; Sleep Functions (Native)
; ============================================================================
(defn sleep [ms] :native)

; ============================================================================
; Byte Array Functions (Native)
; ============================================================================
(defn byte-array [size-or-seq] :native)
(defn aget [array idx] :native)
(defn aset [array idx val] :native)
(defn alength [array] :native)
(defn aclone [array] :native)

; ============================================================================
; Event Loop Functions (Native)
; ============================================================================
(defn run-next-task [] :native)
(defn schedule [ms f] :native)
(defn schedule-periodic [ms f] :native)
(defn cancel-timer [timer-id] :native)

; ============================================================================
; Atom Functions (Native)
; ============================================================================
(defn atom [val] :native)
(defn deref [ref] :native)
(defn reset! [atom val] :native)
(defn swap! [atom f & args] :native)

; ============================================================================
; File I/O Functions (Native, non-ESP32)
; ============================================================================
; Note: slurp and spit are only available on non-ESP32 builds
; They are registered conditionally in builtins.c
^#^{:doc "Reads the entire contents of filename and returns it as a string."}
(defn slurp [filename] :native)
^#^{:doc "Writes content (and optional more strings) to filename, overwriting existing data."}
(defn spit [filename content & more] :native)
)CLOJURE"


R"CLOJURE(
(ns clojure.core)

; ============================================================================
; BOOTSTRAP SECTION: Minimal bootstrap for defn macro
; These must be defined FIRST using (def ... (fn ...)) syntax
; ============================================================================
; Bootstrap primitives required by the defn macro expansion.
; ============================================================================
^#^{:doc "Creates a new list containing the items."}
(def list (fn list [& items] :native))
^#^{:doc "Returns a new seq where x is the first element and seq is the rest."}
(def cons (fn cons [x seq] :native))

; ============================================================================
; Bootstrap helpers needed by defn macro expansion (docstring detection)
; Define these with (def ... (fn ... :native)) so defn macro can run.
; ============================================================================
(def first (fn first [coll] :native))
(def rest (fn rest [coll] :native))
(def next (fn next [coll] :native))
(def count (fn count [coll] :native))
(def vector? (fn vector? [x] :native))
(def list? (fn list? [x] :native))
(def symbol? (fn symbol? [x] :native))
(def nnext (fn nnext [coll] :native))
(def second (fn second [coll] (first (rest coll))))

; ============================================================================
; defn Macro (bootstrap-safe: relies only on helpers defined above)
; ============================================================================
^#^{:doc "Defines a function. Same as (def name (fn name [params] body...)). Supports optional docstring: (defn name \"doc\" [params] body...)"}
(defmacro defn [name & args]
  ; Simple heuristic: if we have 3+ args and first is not a vector/list/symbol, assume it's a docstring
  ; This works because at macro expansion time, string literals are already parsed
  ; We check if first arg looks like params (vector) - if not and we have 3+ args, it's likely a docstring
  (if (and (>= (count args) 3)
           (let [first-arg (first args)]
             (and first-arg 
                  (not (vector? first-arg))
                  (not (list? first-arg))
                  (not (symbol? first-arg)))))
    ; Has docstring: (defn name "doc" [params] body...)
    (let [docstring (first args)
          params (second args)
          body (nnext args)]
      (list 'def name (cons 'fn (cons name (cons params body)))))
    ; No docstring: (defn name [params] body...)
    (let [params (first args)
          body (rest args)]
      (list 'def name (cons 'fn (cons name (cons params body)))))))

; ============================================================================
; Core Collection Functions (must be defined early - used by other functions)
; ============================================================================
^#^{:doc "Returns a sequence of the collection. Returns nil if coll is empty or nil."}
(defn seq [coll] :native)

^#^{:doc "Returns the logical complement of x. Returns true if x is false or nil, false otherwise."}
(defn not [x] :native)

^#^{:doc "Internal helper. Creates a LazySeq from a 0-arity thunk."}
(defn lazy-seq* [f] :native)

^#^{:doc "Takes a body of expressions and yields a LazySeq that will evaluate them once when realized."}
(defmacro lazy-seq [& body]
  (list 'clojure.core/lazy-seq* (cons 'fn (cons [] body))))

^#^{:doc "Creates a new vector containing the args."}
(defn vector [& args] :native)
^#^{:doc "Returns a vector of the contents of coll."}
(defn vec [coll] :native)
^#^{:doc "Returns a lazy sequence of the first n items in coll."}
(defn take [n coll] :native)
^#^{:doc "Returns a lazy sequence of all but the first n items in coll."}
(defn drop [n coll] :native)

; ============================================================================
; Arithmetic Functions (Native) - defined early for use in numeric predicates
; ============================================================================
^#^{:doc "Increments a number by 1. Returns the number plus one."}
(def inc (fn [x] (+ x 1)))
^#^{:doc "Decrements a number by 1. Returns the number minus one."}
(def dec (fn [x] (- x 1)))

; ============================================================================
; Numeric Predicates
; ============================================================================
^#^{:doc "Returns true if x is zero, false otherwise."}
(def zero? (fn [x] (= x 0)))
^#^{:doc "Returns true if x is positive, false otherwise."}
(def pos? (fn [x] (> x 0)))
^#^{:doc "Returns true if x is negative, false otherwise."}
(def neg? (fn [x] (< x 0)))
^#^{:doc "Returns true if x is even, false otherwise."}
(def even? (fn [x] (= (mod x 2) 0)))
^#^{:doc "Returns true if x is odd, false otherwise."}
(def odd? (fn [x] (= (mod x 2) 1)))
^#^{:doc "Returns the maximum of a and b."}
(def max (fn [a b] (if (> a b) a b)))
^#^{:doc "Returns the minimum of a and b."}
(def min (fn [a b] (if (< a b) a b)))

; ============================================================================
; Collection Functions
; ============================================================================
^#^{:doc "Returns true if coll has no items - same as (not (seq coll)). Please use the idiom (seq coll) when testing whether a collection is non-empty."}
(def empty? (fn [coll] 
  (not (seq coll))))
; ============================================================================
; Map Functions (native implementations with docstrings)
; ============================================================================
; Forward declarations removed - these functions are defined later as :native (lines 511-521)
; The :native definitions will be registered when clojure.core.clj is loaded

; ============================================================================
; Utility Functions
; ============================================================================
^#^{:doc "Returns its argument."}
(def identity (fn [x] x))
^#^{:doc "Returns a function that takes one argument and returns x. Note: Variadic functions (& args) are not yet supported, so the returned function accepts only one argument instead of any number of arguments."}
(def constantly (fn [x] (fn [arg] x)))

; ============================================================================
; Higher-Order Functions
; ============================================================================
^#^{:doc "Returns a lazy sequence consisting of the result of applying f to the set of first items of each coll, followed by applying f to the set of second items in each coll, until any one of the colls is exhausted. Any remaining items in other colls are ignored. Function f should accept number-of-colls arguments. Requires at least f and one coll (transducers not supported)."}
(defn map [f & colls] :native)

^#^{:doc "Returns a sequence of the items in coll for which (pred item) returns true. pred must be free of side-effects."}
(defn filter [pred coll] :native)

; ============================================================================
; Type Predicates (needed by Threading Macros)
; ============================================================================
^#^{:doc "Returns true if x is a map, false otherwise."}
(defn map? [x] :native)
^#^{:doc "Returns true if x implements IFn."}
(defn fn? [x] :native)
^#^{:doc "Returns true if x is a Keyword."}
(defn keyword? [x] :native)
^#^{:doc "Returns true if x is a String."}
(defn string? [x] :native)
^#^{:doc "Returns true if x is nil, false otherwise."}
(defn nil? [x] :native)
^#^{:doc "Returns true if x is a kind of persistent set."}
(defn set? [x] :native)
^#^{:doc "Returns true if x is a persistent collection."}
(defn coll? [x] (or (list? x) (vector? x) (map? x) (set? x)))
^#^{:doc "Returns true if x implements ISeq."}
(defn seq? [x] (list? x))
^#^{:doc "Returns true if (seq x) will succeed, false otherwise."}
(defn seqable? [x] (or (nil? x) (coll? x) (string? x)))
^#^{:doc "Returns true if x implements IFn. Note that many data structures implement IFn."}
(defn ifn? [x] (or (fn? x) (keyword? x) (map? x) (vector? x)))

; ============================================================================
; Sequence Helper Functions (needed by Threading Macros)
; ============================================================================
^#^{:doc "Internal 2-arity helper for concat (native)."}
(defn concat2 [x y] :native)

^#^{:doc "Returns the last item in coll, in linear time."}
(defn last [coll]
  (if (empty? coll)
    nil
    (if (empty? (rest coll))
      (first coll)
      (last (rest coll)))))

^#^{:doc "Return a seq of all but the last item in coll, in linear time."}
(defn butlast [coll]
  (if (or (empty? coll) (empty? (rest coll)))
    nil
    (cons (first coll) (butlast (rest coll)))))

^#^{:doc "Returns a lazy seq of the first item in each coll, then the second etc."}
(defn interleave [c1 c2]
  (if (or (empty? c1) (empty? c2))
    (list)
    (cons (first c1)
          (cons (first c2)
                (interleave (rest c1) (rest c2))))))

^#^{:doc "Helper function for threading macros: interleaves a repeated value with a collection"}
(defn interleave-repeat [val coll]
  (if (empty? coll)
    (list)
    (cons val
          (cons (first coll)
                (interleave-repeat val (rest coll))))))

^#^{:doc "Returns a lazy (infinite!, or length n if supplied) sequence of xs."}
(defn repeat [& args]
  (if (= (count args) 1)
    (let [x (first args)]
      (lazy-seq
        (cons x (repeat x))))
    (if (= (count args) 2)
      (let [n (first args)
            x (second args)]
        (let [build (fn build [n]
                      (if (<= n 0)
                        (list)
                        (cons x (build (dec n)))))]
          (vec (build n))))
      (throw "repeat requires 1 or 2 arguments"))))

^#^{:doc "Generate unique symbol names."}
(defn gensym [& prefix] :native)

; ============================================================================
; Macros (sorted by dependencies on other macros)
; ============================================================================
^#^{:doc "Like defn, but marks the resulting var as private to the current namespace."}
(defmacro defn- [name & args]
  (list 'mark-private! (cons 'defn (cons name args))))

^#^{:doc "Returns a vector of the results of calling (map f colls...)."}
(defmacro mapv [f & colls]
  (list 'vec (cons 'map (cons f colls))))

^#^{:doc "Returns a seq representing the concatenation of the elements in colls. Implemented as a macro (no multi-arity support yet) that expands into nested calls to clojure.core/concat2."}
(defmacro concat [& colls]
  (let [build (fn build [xs]
                (if (empty? xs)
                  (list 'clojure.core/list)
                  (if (empty? (rest xs))
                    (list 'clojure.core/seq (first xs))
                    (if (empty? (rest (rest xs)))
                      (list 'clojure.core/concat2 (first xs) (second xs))
                      (list 'clojure.core/concat2 (first xs) (build (rest xs)))))))]
    (build colls)))

^#^{:doc "Threads the expr through the forms. Inserts x as the second item in the first form, making a list of it if it is not a list already. If there are more forms, inserts the first form as the second item in second form, etc."}
(defmacro -> [x & forms]
  (let [thread-step (fn thread-step [x forms]
                      (if (empty? forms)
                        x
                        (let [form (first forms)
                              threaded (if (list? form)
                                         (let [op (first form)
                                               args (rest form)]
                                           (cons op (cons x args)))
                                         (if (symbol? form)
                                           (list form x)
                                           (list 'clojure.core/list form x)))
                              rest-forms (next forms)]
                          (if (nil? rest-forms)
                            threaded
                            (thread-step threaded rest-forms)))))]
    (thread-step x forms)))

^#^{:doc "Threads the expr through the forms. Inserts x as the last item in the first form, making a list of it if it is not a list already. If there are more forms, inserts the first form as the last item in second form, etc."}
(defmacro ->> [x & forms]
  (let [append-last (fn append-last [lst val]
                      (if (empty? lst)
                        (list val)
                        (if (nil? (next lst))
                          (list (first lst) val)
                          (cons (first lst) (append-last (rest lst) val)))))
        thread-step (fn thread-step [x forms]
                      (if (empty? forms)
                        x
                        (let [form (first forms)
                              threaded (if (list? form)
                                         (append-last form x)
                                         (if (symbol? form)
                                           (list form x)
                                           (list 'clojure.core/list form x)))
                              rest-forms (next forms)]
                          (if (nil? rest-forms)
                            threaded
                            (thread-step threaded rest-forms)))))]
    (thread-step x forms)))

^#^{:doc "Binds name to expr, evaluates the first form in the lexical context of that binding, then binds name to that result, repeating for each successive form, returning the result of the last form."}
(defmacro as-> [expr name & forms]
  (list 'let (vec (concat (list name expr) (interleave-repeat name (butlast forms))))
        (if (empty? forms)
          name
          (last forms))))

^#^{:doc "When expr is not nil, threads it into the first form (via ->), and when that result is not nil, through the next etc"}
(defmacro some-> [expr & forms]
  (let [g (gensym)
        build-bindings (fn build-bindings [fs]
                         (if (empty? fs)
                           (list)
                           (let [step (first fs)
                                 threaded (if (keyword? step)
                                            (list 'get g step)
                                            (list '-> g step))
                                 guarded (list 'if (list 'nil? g) 'nil threaded)]
                             (cons g (cons guarded (build-bindings (rest fs)))))))]
    (list 'let (vec (cons g (cons expr (build-bindings forms))))
          g)))

^#^{:doc "When expr is not nil, threads it into the first form (via ->>), and when that result is not nil, through the next etc"}
(defmacro some->> [expr & forms]
  (let [g (gensym)
        build-bindings (fn build-bindings [fs]
                         (if (empty? fs)
                           (list)
                           (let [step (first fs)
                                 threaded (list '->> g step)
                                 guarded (list 'if (list 'nil? g) 'nil threaded)]
                             (cons g (cons guarded (build-bindings (rest fs)))))))]
    (list 'let (vec (cons g (cons expr (build-bindings forms))))
          g)))

^#^{:doc "Takes an expression and a set of test/form pairs. Threads expr (via ->) through each form for which the corresponding test expression is true. Note that, unlike cond branching, cond-> threading does not short circuit after the first true test expression."}
(defmacro cond-> [expr & clauses]
  (if (not (even? (count clauses)))
    (throw "cond-> requires an even number of clauses"))
  (let [g (gensym)
        build-bindings (fn build-bindings [cs]
                         (if (empty? cs)
                           (list)
                           (let [test (first cs)
                                 step (second cs)
                                 guarded (list 'if test (list '-> g step) g)]
                             (cons g (cons guarded (build-bindings (rest (rest cs))))))))]
    (list 'let (vec (cons g (cons expr (build-bindings clauses))))
          g)))

^#^{:doc "Takes an expression and a set of test/form pairs. Threads expr (via ->>) through each form for which the corresponding test expression is true. Note that, unlike cond branching, cond->> threading does not short circuit after the first true test expression."}
(defmacro cond->> [expr & clauses]
  (if (not (even? (count clauses)))
    (throw "cond->> requires an even number of clauses"))
  (let [g (gensym)
        build-bindings (fn build-bindings [cs]
                         (if (empty? cs)
                           (list)
                           (let [test (first cs)
                                 step (second cs)
                                 guarded (list 'if test (list '->> g step) g)]
                             (cons g (cons guarded (build-bindings (rest (rest cs))))))))]
    (list 'let (vec (cons g (cons expr (build-bindings clauses))))
          g)))

; Native Functions - now we can use defn
; ============================================================================
^#^{:doc "Returns the metadata of obj, returns nil if there is no metadata."}
(defn meta [x] :native)
^#^{:doc "Returns an object of the same type and value as obj, with map m as its metadata."}
(defn with-meta [obj m] :native)

^#^{:doc "Returns a snapshot map of current dynamic bindings (symbol -> value)."}
(defn get-thread-bindings [] :native)

; ============================================================================
; Reduce Functions
; ============================================================================
^#^{:doc "f should be a function of 2 arguments. Returns the result of applying f to val and the first item in coll, then applying f to that result and the 2nd item, etc. If coll contains no items, returns val and f is not called."}
(defn reduce [f coll] :native)

; ============================================================================
; Arithmetic Functions (Native) - defined early for use in numeric predicates
; ============================================================================
^#^{:doc "Returns the sum of numbers. (+) returns 0."}
(defn + [& args] :native)
^#^{:doc "Returns the difference of numbers. (- x) returns negation of x. (- x y) returns x minus y."}
(defn - [& args] :native)
^#^{:doc "Returns the product of numbers. (*) returns 1."}
(defn * [& args] :native)
^#^{:doc "Returns the quotient of dividing numerator by denominator(s)."}
(defn / [& args] :native)
^#^{:doc "Modulus of num and div. Truncates toward negative infinity."}
(defn mod [num div] :native)
^#^{:doc "quot[ient] of dividing numerator by denominator."}
(defn quot [num div] :native)
^#^{:doc "Bitwise left shift. Shifts x left by n bits."}
(defn bit-shift-left [x n] :native)
^#^{:doc "Bitwise right shift. Shifts x right by n bits."}
(defn bit-shift-right [x n] :native)
^#^{:doc "Bitwise AND of two integers."}
(defn bit-and [x y] :native)
^#^{:doc "Bitwise OR of two integers."}
(defn bit-or [x y] :native)

; ============================================================================
; Comparison Functions (Native)
; ============================================================================
^#^{:doc "Returns non-nil if nums are in monotonically increasing order, otherwise false."}
(defn < [x y] :native)
^#^{:doc "Returns non-nil if nums are in monotonically decreasing order, otherwise false."}
(defn > [x y] :native)
^#^{:doc "Returns non-nil if nums are in monotonically non-decreasing order, otherwise false."}
(defn <= [x y] :native)
^#^{:doc "Returns non-nil if nums are in monotonically non-increasing order, otherwise false."}
(defn >= [x y] :native)
^#^{:doc "Equality. Returns true if x equals y, false if not. Same as Java x.equals method except it also works for nil, and compares numbers and collections in a type-independent manner. Clojures immutable data structures define equals as a value, not an identity, comparison."}
(defn = [x y] :native)
^#^{:doc "Same as (not (= x y))."}
(defn not= [x y] :native)
^#^{:doc "Tests if 2 arguments are the same object."}
(defn identical? [x y] :native)

; ============================================================================
; Core Collection Functions (Native)
; ============================================================================
^#^{:doc "Returns the square root of x."}
(defn sqrt [x] :native)

; ============================================================================
; String Functions (Native)
; ============================================================================
^#^{:doc "With no args, returns the empty string. With one arg x, returns x.toString(). (str nil) returns empty string. With more than one arg, returns the concatenation of str values of the args."}
(defn str [& args] :native)
^#^{:doc "Returns the substring of s beginning at start inclusive, and ending at end (defaults to length of string), exclusive."}
(defn subs [s start & end] :native)

; ============================================================================
; Symbol Functions (Native)
; ============================================================================
^#^{:doc "Returns a Symbol with the given namespace and name."}
(defn symbol [& args] :native)

; ============================================================================
; Type Functions (Native)
; ============================================================================
^#^{:doc "Returns the type of x."}
(defn type [x] :native)

; ============================================================================
; Collection Functions (Native)
; ============================================================================
^#^{:doc "Returns the value at the index. get returns nil if index out of bounds, nth throws an exception unless not-found is supplied. nth also works for strings, Java arrays, regex Matchers and Lists, and, in O(n) time, for sequences."}
(defn nth [coll index & not-found] :native)
^#^{:doc "For a list or queue, same as first, for a vector, same as, but much more efficient than, last. If the collection is empty, returns nil."}
(defn peek [coll] :native)
^#^{:doc "For a list or queue, returns a new list/queue without the first item, for a vector, returns a new vector without the last item. If the collection is empty, returns nil."}
(defn pop [coll] :native)
^#^{:doc "Returns a persistent vector of the items in vector from start (inclusive) to end (exclusive). If end is not supplied, defaults to (count vector)."}
(defn subvec [v start & end] :native)
^#^{:doc "Returns a new collection consisting of coll with the xs 'added'. (conj coll item) adds item at an appropriate 'place' in the collection. For lists, conj prepends. For vectors, conj appends."}
(defn conj [coll & items] :native)
^#^{:doc "Returns a set of the distinct items."}
(defn hash-set [& items] :native)
^#^{:doc "Disjoints. Returns a new set that does not contain the keys."}
(defn disj [set & keys] :native)
^#^{:doc "Returns a seq of the items in coll in reverse order. Not lazy."}
(defn reverse [coll] :native)

; ============================================================================
; Predicate Functions (Native)
; ============================================================================
; ============================================================================
; Type Predicates (Native)
; ============================================================================
^#^{:doc "Returns true if x is a Number."}
(defn number? [x] :native)
^#^{:doc "Returns true if n is an integer (fixed precision)."}
(defn integer? [x] :native)
^#^{:doc "Returns true if n is a floating point number."}
(defn float? [x] :native)
^#^{:doc "Returns true if x is a Character."}
(defn char? [x] :native)

; ============================================================================
; Boolean Functions (Native)
; ============================================================================
^#^{:doc "Returns true if x is not nil, false otherwise."}
(defn some? [x] (not (nil? x)))
^#^{:doc "Returns true if x is the value true, false otherwise."}
(defn true? [x] (identical? x true))
^#^{:doc "Returns true if x is the value false, false otherwise."}
(defn false? [x] (identical? x false))
^#^{:doc "Returns true if x is a Boolean."}
(defn boolean? [x] (or (true? x) (false? x)))

; ============================================================================
; Map Functions (Native)
; ============================================================================
^#^{:doc "Returns a new array map with supplied mappings."}
(defn array-map [& keyvals] :native)
^#^{:doc "Returns a new hash map with supplied mappings."}
(defn hash-map [& keyvals] :native)
^#^{:doc "Associates key with val in map. When key is a keyword, returns a new map with the key/value added. When key is not a keyword, returns a new map with the key/value added. If a key already exists, its value is replaced."}
(defn assoc [map key val & kvs] :native)
^#^{:doc "Dissociates. Returns a new map of the same (hashed/sorted) type, that does not contain a mapping for key(s)."}
(defn dissoc [map & keys] :native)
^#^{:doc "Gets the value mapped to key, not-found or nil if key not present."}
(defn get [map key & not-found] :native)
^#^{:doc "Returns a sequence of the map's keys, in the same order as (seq map)."}
(defn keys [map] :native)
^#^{:doc "Returns a sequence of the map's values, in the same order as (seq map)."}
(defn vals [map] :native)
^#^{:doc "Returns a map that consists of the rest of the maps conj-ed onto the first. If a key occurs in more than one map, the mapping from the latter (left-to-right) will be the mapping in the result."}
(defn merge [& maps] :native)
^#^{:doc "Returns true if key is present in the given collection, otherwise returns false. Note that for numerically indexed collections like vectors, this tests if the numeric key is within the range of indexes."}
(defn contains? [coll key] :native)
^#^{:doc "Returns a new coll consisting of to-coll with all of the items of from-coll conjoined."}
(defn into [to from] :native)
^#^{:doc "Returns a map containing only those entries in map whose key is in keys."}
(defn select-keys [map ks] :native)
^#^{:doc "Returns the map entry for key, or nil if key not present."}
(defn find [map key] :native)
^#^{:doc "Updates a value in an associative structure, where k is a key and f is a function that will take the old value and any supplied args and return the new value. If the key does not exist, nil is passed as the old value."}
(defn update [map key f & args] :native)
^#^{:doc "Registers a record descriptor for a type symbol and ordered field list."}
(defn record-register [type-name fields] :native)
^#^{:doc "Creates a record instance from type symbol and ordered values."}
(defn record-create [type-name values] :native)
^#^{:doc "Creates a record instance from type symbol and map-like source."}
(defn record-from-map [type-name m] :native)
^#^{:doc "Internal fast-path: record lookup by precomputed field index."}
(defn record-get-index [record idx not-found] :native)

; ============================================================================
; Transient Functions (Native)
; ============================================================================
^#^{:doc "Returns a new, transient version of the collection, in constant time."}
(defn transient [coll] :native)
^#^{:doc "Returns a new, persistent version of the transient collection, in constant time. The transient collection cannot be used after this call, any such use will throw an exception."}
(defn persistent! [tcoll] :native)
^#^{:doc "Adds val to the transient collection, and return coll. The 'addition' may happen at different 'places' depending on the concrete type."}
(defn conj! [tcoll val] :native)

; ============================================================================
; Print Functions (Native)
; ============================================================================
^#^{:doc "Prints the object(s) to the output stream that is the current value of *out*. Prints a newline at the end. Returns nil."}
(defn println [& args] :native)
^#^{:doc "Prints the object(s) to the output stream that is the current value of *out*. Returns nil."}
(defn print [& args] :native)
^#^{:doc "Prints the object(s) to the output stream that is the current value of *out*. pr and prn produce output for programs to read. They do not add whitespace between objects, and they produce representations that are readably by the reader."}
(defn pr [& args] :native)
^#^{:doc "Same as pr followed by (println)."}
(defn prn [& args] :native)

; ============================================================================
; String Functions (Native)
; ============================================================================
^#^{:doc "Formats a string using java.lang.String.format, see java.util.Formatter for format string syntax."}
(defn format [fmt & args] :native)

; ============================================================================
; Symbol/Keyword Functions (Native)
; ============================================================================
^#^{:doc "Returns a Keyword with the given namespace and name."}
(defn keyword [& args] :native)
^#^{:doc "Returns the name String of a string, symbol or keyword."}
(defn name [x] :native)

^#^{:doc "Defines a record type with compact field layout plus ->Type/map->Type constructors."}
(defmacro defrecord [type-name fields]
  (let [ctor (symbol (str "->" (name type-name)))
        map-ctor (symbol (str "map->" (name type-name)))
        m (symbol "m")
        ctor-body (list 'record-create (list 'quote type-name) fields)
        map-body (list 'record-from-map (list 'quote type-name) m)]
    (list 'do
          (list 'record-register (list 'quote type-name) (list 'quote fields))
          (list 'def ctor (list 'fn ctor fields ctor-body))
          (list 'def map-ctor (list 'fn map-ctor [m] map-body))
          (list 'quote type-name))))

; ============================================================================
; Sequence Functions (Native)
; ============================================================================
^#^{:doc "Returns a lazy seq of numbers from start (inclusive) to end (exclusive), by step, where start defaults to 0, step to 1, and end to infinity."}
(defn range [& args] :native)
^#^{:doc "Returns the nth next of coll, (seq coll) when n is 0."}
(defn nthnext [coll n] :native)
^#^{:doc "Returns the first logical true value of (pred x) for any x in coll, else nil. Native implementation."}
(defn some [pred coll] :native)
^#^{:doc "Returns a lazy sequence of lists of n items each. Native implementation."}
(defn partition [n coll] :native)

; ============================================================================
; Control Flow Functions (Native)
; ============================================================================
^#^{:doc "Evaluates expressions in order and returns the value of the last. If no expressions are supplied, returns nil."}
(defn do [& exprs] :native)

; ============================================================================
; Namespace Functions (Native)
; ============================================================================
^#^{:doc "Returns a map of all the mappings for the namespace."}
(defn ns-map [ns] :native)
^#^{:doc "Returns the namespace named by the symbol or nil if it doesn't exist."}
(defn find-ns [sym] :native)
^#^{:doc "Returns a sequence of all namespaces currently loaded."}
(defn all-ns [] :native)
^#^{:doc "Unloads a namespace by name, removing it from the registry and releasing its mappings (symbols remain interned). Returns true if unloaded, false if not found."}
(defn ns-unload [ns] :native)

; ============================================================================
; Time + Yield Functions
; ============================================================================
^#^{:doc "Yields control to the platform event loop for up to ms milliseconds. Returns nil."}
(defn yield [ms] :native)

^#^{:doc "Returns milliseconds since start of the current UTC day [0..86400000)."}
(defn current-time-ms [] :native)

^#^{:doc "Causes the current thread to sleep for ms milliseconds while continuing to drive the platform event loop. API compatible with Thread/sleep from Clojure JVM."}
(defn sleep [ms]
  (let [end-ms (+ (current-time-ms) ms)]
    (loop []
      (let [remaining (- end-ms (current-time-ms))]
        (when (pos? remaining)
          (yield remaining)
          (recur))))))

; ============================================================================
; Byte Array Functions (Native)
; ============================================================================
^#^{:doc "Creates an array of bytes. If size-or-seq is a number, creates a byte array of that size. If size-or-seq is a sequence, creates a byte array with the elements of the sequence."}
(defn byte-array [size-or-seq] :native)
^#^{:doc "Returns the value at the index. Works on Java arrays. Note - aget throws an exception if the index is out of bounds."}
(defn aget [array idx] :native)
^#^{:doc "Sets the value at the index. Works on Java arrays. Note - aset throws an exception if the index is out of bounds."}
(defn aset [array idx val] :native)
^#^{:doc "Returns the length of the Java array. Works on arrays of all types."}
(defn alength [array] :native)
^#^{:doc "Returns a clone of the Java array. Works on arrays of all types."}
(defn aclone [array] :native)

; ============================================================================
; Event Loop Functions (Native)
; ============================================================================
^#^{:doc "Runs the next task in the event loop queue, if any. Returns true if a task was run, false otherwise."}
(defn run-next-task [] :native)
^#^{:doc "Schedules work after ms milliseconds. Second arg can be a function f, or options map {:fn f :id k :period-ms p}. With :id, scheduling upserts by equal key. If :period-ms > 0, the timer is periodic."}
(defn schedule [ms f] :native)
^#^{:doc "Schedules periodic work. Third arg can be function f or options map {:fn f :id k}. With :id, scheduling upserts by equal key."}
(defn schedule-periodic [delay-ms period-ms f] :native)
^#^{:doc "Cancels a timer by numeric timer-id or by named key. Returns true if cancelled, false otherwise."}
(defn cancel-timer [timer-id] :native)

; ============================================================================
; Atom Functions (Native)
; ============================================================================
^#^{:doc "Creates an atom with an initial value val and returns it."}
(defn atom [val] :native)
^#^{:doc "Dereferences a ref, atom, or var, returning its current value."}
(defn deref [ref] :native)
^#^{:doc "Sets the value of atom to val without regard for the current value. Returns val."}
(defn reset! [atom val] :native)
^#^{:doc "Atomically swaps the value of atom to be (apply f current-value-of-atom args). Note that f may be called multiple times, and thus should be free of side effects. Returns the value that was swapped in."}
(defn swap! [atom f & args] :native)

; ============================================================================
; File I/O Functions (Native)
; ============================================================================
; Note: slurp resolves embedded sources on ESP32. spit writes via the KV-backed FS layer.
^#^{:doc "Reads the entire contents of filename and returns it as a string."}
(defn slurp [filename] :native)
^#^{:doc "Writes content (and optional more strings) to filename, overwriting existing data."}
(defn spit [filename content & more] :native)

; ============================================================================
; END OF NATIVE FUNCTIONS
; ============================================================================

; ============================================================================
; Derived Functions (using native functions defined above)
; ============================================================================

; ============================================================================
; Predicate Functions (Phase 2)
; ============================================================================

^#^{:doc "Returns true if (pred x) is logical true for every x in coll, else false."}
(defn every? [pred coll]
  (if (empty? coll)
    true
    (if (pred (first coll))
      (every? pred (rest coll))
      false)))

^#^{:doc "Returns false if (pred x) is logical true for every x in coll, else true."}
(defn not-every? [pred coll]
  (not (every? pred coll)))

^#^{:doc "Returns false if (pred x) is logical true for any x in coll, else true."}
(defn not-any? [pred coll]
  (not (some pred coll)))

; ============================================================================
; Higher-Order Sequence Functions (Phase 3)
; ============================================================================

^#^{:doc "Returns the result of applying concat to the result of applying map to f and colls."}
(defn mapcat [f coll] :native)

^#^{:doc "Returns a lazy sequence of successive items from coll while (pred item) returns logical true."}
(defn take-while [pred coll]
  (lazy-seq
    (if (empty? coll)
      (list)
      (if (pred (first coll))
        (cons (first coll) (take-while pred (rest coll)))
        (list)))))

^#^{:doc "Returns a lazy sequence of the items in coll starting from the first item for which (pred item) returns logical false."}
(defn drop-while [pred coll]
  (lazy-seq
    (if (empty? coll)
      (list)
      (if (pred (first coll))
        (drop-while pred (rest coll))
        coll))))

^#^{:doc "Returns a lazy sequence of the non-nil results of (f item)."}
(defn keep [f coll]
  (lazy-seq
    (if (empty? coll)
      (list)
      (let [result (f (first coll))]
        (if (nil? result)
          (keep f (rest coll))
          (cons result (keep f (rest coll))))))))

; ============================================================================
; Aggregation Functions (Phase 4)
; ============================================================================

^#^{:doc "Returns a lazy seq of the intermediate values of the reduction."}
(defn reductions [f init coll]
  (cons init
        (if (empty? coll)
          (list)
          (reductions f (f init (first coll)) (rest coll)))))

^#^{:doc "Returns a map from distinct items in coll to the number of times they appear."}
(defn frequencies [coll]
  (reduce (fn [counts x]
            (assoc counts x (inc (get counts x 0))))
          {} coll))

^#^{:doc "Returns a map of the elements of coll keyed by the result of f on each element."}
(defn group-by [f coll] :native)

^#^{:doc "Returns a lazy sequence of the elements of coll with duplicates removed."}
(defn distinct [coll]
  (loop [seen {}
         xs coll
         out []]
    (if (empty? xs)
      (seq out)
      (let [x (first xs)]
        (if (get seen x)
          (recur seen (rest xs) out)
          (recur (assoc seen x true)
                 (rest xs)
                 (conj out x)))))))

; ============================================================================
; Partitioning Functions (Phase 5)
; ============================================================================

^#^{:doc "Returns a lazy sequence of lists like partition, but may include partitions with fewer than n items at the end."}
(defn partition-all [n coll]
  (if (empty? coll)
    (list)
    (cons (vec (take n coll)) (partition-all n (drop n coll)))))

^#^{:doc "Returns a vector of [(take n coll) (drop n coll)]."}
(defn split-at [n coll]
  [(vec (take n coll)) (vec (drop n coll))])

^#^{:doc "Returns a vector of [(take-while pred coll) (drop-while pred coll)]."}
(defn split-with [pred coll]
  [(vec (take-while pred coll)) (vec (drop-while pred coll))])

; ============================================================================
; Map Construction Functions (Phase 6)
; ============================================================================

^#^{:doc "Returns a map with the keys mapped to the corresponding vals."}
(defn zipmap [ks vs]
  (loop [m {}
         ks ks
         vs vs]
    (if (or (empty? ks) (empty? vs))
      m
      (recur (assoc m (first ks) (first vs))
             (rest ks)
             (rest vs)))))

^#^{:doc "Returns the value in a nested associative structure."}
(defn get-in [m ks]
  (reduce (fn [m k]
            (if (nil? m)
              nil
              (get m k)))
          m ks))

; ============================================================================
; Function Composition (Phase 7)
; ============================================================================

^#^{:doc "Takes a function f and fewer than the normal arguments to f, and returns a fn that takes a variable number of additional args."}
(defn partial [f arg1]
  (fn [x] (f arg1 x)))

^#^{:doc "Takes a set of functions and returns a fn that is the composition of those fns."}
(defn comp [f g]
  (fn [x] (f (g x))))

^#^{:doc "Takes a set of functions and returns a fn that is the juxtaposition of those fns."}
(defn juxt [f g]
  (fn [x] [(f x) (g x)]))

^#^{:doc "Takes a fn f and returns a fn that takes the same arguments as f, has the same effects, if any, and returns the opposite truth value."}
(defn complement [f]
  (fn [x] (not (f x))))

; ============================================================================
; Iteration Functions (Phase 8)
; ============================================================================

^#^{:doc "Takes a function of no args, presumably with side effects, and returns an infinite lazy sequence of calls to it."}
(defn repeatedly [n f]
  (if (<= n 0)
    (list)
    (cons (f) (repeatedly (dec n) f))))

^#^{:doc "Reduces an associative collection. f should be a function of 3 arguments."}
(defn reduce-kv [f init m]
  (if (nil? m)
    init
    (reduce (fn [acc k]
              (f acc k (get m k)))
            init (keys m))))

^#^{:doc "Returns the absolute value of a."}
(defn abs [x]
  (if (< x 0) (- x) x))

^#^{:doc "Remainder of dividing numerator by denominator."}
(defn rem [num div]
  (- num (* div (quot num div))))

; ============================================================================
; Destructuring Support (bootstrap-safe: no destructuring in implementation)
; ============================================================================

^#^{:doc "Transforms binding forms with destructuring into flat symbol bindings."}
(defn destructure [bindings] :native)

; ============================================================================
; for Macro Helper Function and Macro (requires destructure)
; ============================================================================
; TEST: helper as global function to see if it fixes the nesting problem
(defn normalize-for-bindings-helper [result remaining]
  (loop [result result remaining remaining]
    (if (empty? remaining)
      result
      (let [item (first remaining)]
        (cond
          (= item :when)
          (recur (conj result :when (second remaining)) (nnext remaining))
          (= item :while)
          (recur (conj result :while (second remaining)) (nnext remaining))
          (= item :let)
          (let [let-bindings (second remaining)
                simple-let? (loop [bs (seq let-bindings)]
                              (if (empty? bs)
                                true
                                (if (symbol? (first bs))
                                  (recur (nnext bs))
                                  false)))
                flat-bindings (if simple-let?
                                let-bindings
                                (vec (destructure let-bindings)))]
            (recur (conj result :let flat-bindings) (nnext remaining)))
          :else
          (let [pattern item
                expr (second remaining)]
            (if (symbol? pattern)
              (recur (conj result pattern expr) (nnext remaining))
              (let [g (gensym "for__")
                    destructured (destructure [pattern g])
                    destructured-vec (vec destructured)]
                (recur (conj (conj (conj (conj result g) expr) :let) destructured-vec)
                       (nnext remaining))))))))))

(defn normalize-for-bindings [bindings]
  (normalize-for-bindings-helper [] (seq bindings)))

; Macro builder for `for` (returns a form that produces a lazy seq)
(def for-build
  (fn for-build [clauses body]
    (if (empty? clauses)
      (list 'list body)
      (let [sym (first clauses)
            expr (second clauses)
            more (nnext clauses)
            base-coll expr
            parsed (loop [coll base-coll
                          cs more
                          lets []]
                     (if (or (empty? cs) (not (keyword? (first cs))))
                       (vector coll cs lets)
                       (let [kw (first cs)]
                         (cond
                           (= kw :when)
                             (recur
                              (list 'filter
                                     (list 'fn (vec [sym]) (second cs))
                                     coll)
                              (nnext cs)
                              lets)
                           (= kw :while)
                             (recur
                              (list 'take-while
                                     (list 'fn (vec [sym]) (second cs))
                                     coll)
                              (nnext cs)
                              lets)
                           (= kw :let)
                             (recur coll (nnext cs) (conj lets (second cs)))
                           :else
                             (throw (str "for: unknown binding modifier: " kw))))))]
        (let [coll2 (nth parsed 0)
              rest-clauses (nth parsed 1)
              lets (nth parsed 2)
              inner (for-build rest-clauses body)
              inner-with-lets (loop [acc inner
                                     i (dec (count lets))]
                                (if (< i 0)
                                  acc
                                  (recur (list 'let (nth lets i) acc)
                                         (dec i))))]
          (list 'mapcat
                (list 'fn (vec [sym]) inner-with-lets)
                coll2))))))

^#^{:doc "List comprehension. Expands to nested mapcat/filter/take-while over normalized bindings (supports :when/:let/:while and destructuring)."}
(defmacro for [bindings body]
  (for-build (seq (normalize-for-bindings bindings)) body))

; ============================================================================
; Macro Expansion Functions (bootstrap-safe: uses only basic special forms)
; ============================================================================

^#^{:doc "If form represents a macro call, returns its macro expansion, else returns form."}
(def macroexpand-1
  (fn [form]
    (if (list? form)
      (let [op (first form)]
        (if (symbol? op)
          (let [macro-fn (get-macro op)]
            (if macro-fn
              (apply macro-fn (rest form))
              form))
          form))
      form)))

^#^{:doc "Repeatedly calls macroexpand-1 on form until it no longer represents a macro form."}
(def macroexpand
  (fn [form]
    (loop [current form]
      (let [expanded (macroexpand-1 current)]
        (if (identical? expanded current)
          current
          (recur expanded))))))

; ============================================================================
; Quasiquote Implementation (bootstrap-safe: uses only basic special forms)
; ============================================================================

^#^{:doc "Transforms quasiquote forms with unquote and unquote-splice.
          Handles nested quasiquotes and splicing."}
(def quasiquote-fn
  (fn [form]
    (let [unquote? (fn [x]
                     (and (list? x)
                          (= (name (first x)) "unquote")))
          unquote-splice? (fn [x]
                            (and (list? x)
                                 (= (name (first x)) "unquote-splice")))]
      (cond
        ; Check for unquote: (unquote x) -> x
        (unquote? form)
          (second form)

        ; Check for unquote-splice: (unquote-splice x) -> error (must be inside list)
        (unquote-splice? form)
          (throw (str "unquote-splice not in list context: " form))

        ; Handle lists with potential splicing
        (list? form)
          (let [process-elem (fn [acc elem]
                               (cond
                                 (unquote-splice? elem)
                                   ; Splice: concat2 acc with elements of (second elem)
                                   ; Use clojure.core/concat2 directly (macro-free) because quasiquote
                                   ; evaluates the builder expression at runtime.
                                  (list 'concat2 acc (list 'seq (second elem)))

                                 :else
                                   ; Normal element: append one element in order
                                  (list 'concat2
                                        acc
                                        (list 'list
                                        (quasiquote-fn elem)))))]
            (reduce process-elem (list 'list) form))

        ; Handle vectors - similar to lists but return vector
        (vector? form)
          (list 'vec (list 'quasiquote-fn (list 'quote (seq form))))

        ; Handle maps - quote keys and values
        (map? form)
          (list 'into {} (list 'map (fn [[k v]] [(list 'quasiquote-fn (list 'quote k))
                                                  (list 'quasiquote-fn (list 'quote v))])
                               (seq form)))

        ; Symbols, keywords, and other atoms - quote them
        :else
          (list 'quote form)))))

; ============================================================================
; Atom Watchers
; ============================================================================

; Watcher registry: Atom -> Map of Key -> WatchFn
(def watcher-registry (atom {}))

^#^{:doc "Returns true if x is an Atom."}
(defn atom? [x] :native)

; Helper: Get watcher map for an atom (single lookup point)
(defn get-watcher-map [a]
  (get (deref watcher-registry) a))

; Helper: Update watcher map for an atom (single update point)
(defn update-watcher-map [a f]
  (swap! watcher-registry
         (fn [registry]
           (let [wm (f (get registry a {}))]
             (if (or (nil? wm) (empty? wm))
               (dissoc registry a)
               (assoc registry a wm))))))

^#^{:doc "Adds a watch function to an atom. Returns the atom (for threading).

The watch function will be called with 4 arguments: key, atom, old-value, new-value."}
(defn add-watch [a key watch-fn]
  (if (not (atom? a))
    (throw "add-watch requires an atom")
    nil)
  (if (not (fn? watch-fn))
    (throw "add-watch requires a function as third argument")
    nil)
  (clojure.core/update-watcher-map a (fn [m] (assoc m key watch-fn)))
  a)

^#^{:doc "Removes a watch function from an atom. Returns the atom (for threading)."}
(defn remove-watch [a key]
  (if (not (atom? a))
    (throw "remove-watch requires an atom")
    nil)
  (clojure.core/update-watcher-map a (fn [m] (dissoc m key)))
  a)

^#^{:doc "Notifies all watchers registered on atom a.

This is called synchronously from atom operations (reset!/swap!)."}
(defn notify-watchers [a old-value new-value]
  (let [wm (clojure.core/get-watcher-map a)]
    (if wm
      (reduce-kv
        (fn [acc k f]
          (try
            (f k a old-value new-value)
            (catch Exception e
              (println "Watcher error:" e)))
          acc)
        nil
        wm)
      nil)))

; ============================================================================
; JVM compatibility shims
; ============================================================================
(ns Thread)
(defn sleep [ms]
  (let [end-ms (+ (current-time-ms) ms)]
    (loop []
      (let [remaining (- end-ms (current-time-ms))]
        (when (pos? remaining)
          (yield remaining)
          (recur))))))
(ns clojure.core)
)CLOJURE"

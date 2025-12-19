
R"CLOJURE(
(ns clojure.core)

; ============================================================================
; Tiny-CLJ Core Functions
; ============================================================================

; ============================================================================
; Arithmetic Functions
; ============================================================================
^#^{:doc "Returns the sum of a and b."}
(def add (fn [a b] (+ a b)))
^#^{:doc "Returns the difference of a and b."}
(def sub (fn [a b] (- a b)))
^#^{:doc "Returns the product of a and b."}
(def mul (fn [a b] (* a b)))
^#^{:doc "Returns the quotient of a and b."}
(def div (fn [a b] (/ a b)))
^#^{:doc "Increments a number by 1. Returns the number plus one."}
(def inc (fn [x] (+ x 1)))
^#^{:doc "Decrements a number by 1. Returns the number minus one."}
(def dec (fn [x] (- x 1)))
^#^{:doc "Returns the square of x (x * x)."}
(def square (fn [x] (* x x)))

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
(def odd? (fn [x] (not (= (mod x 2) 0))))

; ============================================================================
; Comparison & Logic
; ============================================================================
^#^{:doc "Returns true if x is logical false, false otherwise."}
(def not (fn [x] (if x false true)))
^#^{:doc "Returns the maximum of a and b."}
(def max (fn [a b] (if (> a b) a b)))
^#^{:doc "Returns the minimum of a and b."}
(def min (fn [a b] (if (< a b) a b)))

; ============================================================================
; Collection Functions
; ============================================================================
^#^{:doc "Returns the second item in coll. Returns nil if coll contains less than 2 items."}
(def second (fn [coll] (first (rest coll))))
^#^{:doc "Returns true if coll has no items - same as (not (seq coll)). Please use the idiom (seq coll) when testing whether a collection is non-empty."}
(def empty? (fn [coll] 
  (if coll
    (= (count coll) 0)
    true)))
; ============================================================================
; Map Functions (native implementations with docstrings)
; ============================================================================
^#^{:doc "Returns a map that consists of the rest of the maps conj-ed onto the first. If a key occurs in more than one map, the mapping from the latter (left-to-right) will be the mapping in the result."}
(def merge merge)

^#^{:doc "Returns true if key is present in the given collection, otherwise returns false. Note that for numerically indexed collections like vectors, this tests if the numeric key is within the range of indexes."}
(def contains? contains?)

^#^{:doc "Returns a new coll consisting of to-coll with all of the items of from-coll conjoined."}
(def into into)

^#^{:doc "Returns a map containing only those entries in map whose key is in keys."}
(def select-keys select-keys)

^#^{:doc "Returns the map entry for key, or nil if key not present."}
(def find find)

^#^{:doc "Updates a value in an associative structure, where k is a key and f is a function that will take the old value and any supplied args and return the new value. If the key does not exist, nil is passed as the old value."}
(def update update)

; ============================================================================
; Utility Functions
; ============================================================================
^#^{:doc "Returns its argument."}
(def identity (fn [x] x))

; ============================================================================
; Higher-Order Functions
; ============================================================================
^#^{:doc "Returns a lazy sequence consisting of the result of applying f to the set of first items of each coll, followed by applying f to the set of second items in each coll, until any one of the colls is exhausted. Any remaining items in other colls are ignored. Function f should accept number-of-colls arguments. Returns a transducer when no collection is provided."}
(def map (fn [f coll]
  (if (empty? coll)
    (list)
    (cons (f (first coll)) (map f (rest coll))))))

^#^{:doc "Returns a lazy sequence of the items in coll for which (pred item) returns true. pred must be free of side-effects. Returns a transducer when no collection is provided."}
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
^#^{:doc "Returns a function that takes one argument and returns x. Note: Variadic functions (& args) are not yet supported, so the returned function accepts only one argument instead of any number of arguments."}
(def constantly (fn [x] (fn [arg] x)))

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

; ============================================================================
; Sequence Functions (Native)
; ============================================================================
^#^{:doc "Returns a lazy seq of numbers from start (inclusive) to end (exclusive), by step, where start defaults to 0, step to 1, and end to infinity."}
(defn range [& args] :native)
^#^{:doc "Returns a lazy (infinite!, or length n if supplied) sequence of xs."}
(defn repeat [x] :native)

; ============================================================================
; Math Functions (Native)
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
^#^{:doc "Returns a new array map with supplied mappings."}
(defn array-map [& keyvals] :native)
^#^{:doc "Creates a new vector containing the args."}
(defn vector [& args] :native)
^#^{:doc "Returns a vector of the contents of coll."}
(defn vec [coll] :native)
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
^#^{:doc "Returns the first item in the collection. Calls seq on its argument. If coll is nil, returns nil."}
(defn first [coll] :native)
^#^{:doc "Returns a sequence of the items after the first. Calls seq on its argument. If there are no more items, returns nil."}
(defn rest [coll] :native)
^#^{:doc "Returns a sequence of the items after the first. Calls seq on its argument. If there are no more items, returns nil (not a sequence)."}
(defn next [coll] :native)
^#^{:doc "Returns a new seq where x is the first element and seq is the rest."}
(defn cons [x seq] :native)
^#^{:doc "Creates a new list containing the items."}
(defn list [& items] :native)
^#^{:doc "Returns the number of items in the collection. (count nil) returns 0. Also works on strings, arrays, and Java Collections."}
(defn count [coll] :native)
^#^{:doc "Returns a seq of the items in coll in reverse order. Not lazy."}
(defn reverse [coll] :native)
^#^{:doc "Associates key with val in map. When key is a keyword, returns a new map with the key/value added. When key is not a keyword, returns a new map with the key/value added. If a key already exists, its value is replaced."}
(defn assoc [map key val & kvs] :native)
^#^{:doc "Dissociates. Returns a new map of the same (hashed/sorted) type, that does not contain a mapping for key(s)."}
(defn dissoc [map & keys] :native)
^#^{:doc "Returns a new, transient version of the collection, in constant time."}
(defn transient [coll] :native)
^#^{:doc "Returns a new, persistent version of the transient collection, in constant time. The transient collection cannot be used after this call, any such use will throw an exception."}
(defn persistent! [tcoll] :native)
^#^{:doc "Adds val to the transient collection, and return coll. The 'addition' may happen at different 'places' depending on the concrete type."}
(defn conj! [tcoll val] :native)
^#^{:doc "Gets the value mapped to key, not-found or nil if key not present."}
(defn get [map key & not-found] :native)
^#^{:doc "Returns a sequence of the map's keys, in the same order as (seq map)."}
(defn keys [map] :native)
^#^{:doc "Returns a sequence of the map's values, in the same order as (seq map)."}
(defn vals [map] :native)

; ============================================================================
; Predicate Functions (Native)
; ============================================================================
^#^{:doc "Returns true if x is nil, false otherwise."}
(defn nil? [x] :native)
^#^{:doc "Returns true if x is a vector, false otherwise."}
(defn vector? [x] :native)
^#^{:doc "Returns true if x is a map, false otherwise."}
(defn map? [x] :native)

; ============================================================================
; Type Predicates (Native)
; ============================================================================
^#^{:doc "Returns true if x is a Number."}
(defn number? [x] :native)
^#^{:doc "Returns true if n is an integer (fixed precision)."}
(defn integer? [x] :native)
^#^{:doc "Returns true if n is a floating point number."}
(defn float? [x] :native)
^#^{:doc "Returns true if x is a String."}
(defn string? [x] :native)
^#^{:doc "Returns true if x is a Keyword."}
(defn keyword? [x] :native)
^#^{:doc "Returns true if x is a Symbol."}
(defn symbol? [x] :native)
^#^{:doc "Returns true if x implements IFn."}
(defn fn? [x] :native)
^#^{:doc "Returns true if x is a Character."}
(defn char? [x] :native)

; ============================================================================
; Type Predicates (Clojure-based)
; ============================================================================
^#^{:doc "Returns true if x is not nil, false otherwise."}
(defn some? [x] (not (nil? x)))
^#^{:doc "Returns true if x is the value true, false otherwise."}
(defn true? [x] (identical? x true))
^#^{:doc "Returns true if x is the value false, false otherwise."}
(defn false? [x] (identical? x false))
^#^{:doc "Returns true if x is a Boolean."}
(defn boolean? [x] (or (true? x) (false? x)))
^#^{:doc "Returns true if x is a kind of persistent list."}
(defn list? [x] :native)
^#^{:doc "Returns true if x is a kind of persistent set."}
(defn set? [x] false)
^#^{:doc "Returns true if x is a persistent collection."}
(defn coll? [x] (or (list? x) (vector? x) (map? x)))
^#^{:doc "Returns true if x implements ISeq."}
(defn seq? [x] (list? x))
^#^{:doc "Returns true if (seq x) will succeed, false otherwise."}
(defn seqable? [x] (or (nil? x) (coll? x) (string? x)))
^#^{:doc "Returns true if x implements IFn. Note that many data structures implement IFn."}
(defn ifn? [x] (or (fn? x) (keyword? x) (map? x) (vector? x)))

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
; Format Functions (Native)
; ============================================================================
^#^{:doc "Formats a string using java.lang.String.format, see java.util.Formatter for format string syntax."}
(defn format [fmt & args] :native)

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

; ============================================================================
; Sleep Functions (Native)
; ============================================================================
^#^{:doc "Causes the current thread to sleep for ms milliseconds."}
(defn sleep [ms] :native)

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
^#^{:doc "Schedules a function f to be called after ms milliseconds. Returns a timer-id that can be used to cancel the timer."}
(defn schedule [ms f] :native)
^#^{:doc "Schedules a function f to be called periodically every ms milliseconds. Returns a timer-id that can be used to cancel the timer."}
(defn schedule-periodic [ms f] :native)
^#^{:doc "Cancels a timer identified by timer-id. Returns true if the timer was cancelled, false otherwise."}
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
; File I/O Functions (Native, non-ESP32)
; ============================================================================
; Note: slurp and spit are only available on non-ESP32 builds
; They are registered conditionally in builtins.c
^#^{:doc "Reads the entire contents of filename and returns it as a string."}
(defn slurp [filename] :native)
^#^{:doc "Writes content (and optional more strings) to filename, overwriting existing data."}
(defn spit [filename content & more] :native)

; ============================================================================
; Sequence Functions (Phase 1)
; ============================================================================

^#^{:doc "Returns a lazy seq representing the concatenation of the elements in x and y."}
(defn concat [x y]
  (if (empty? x)
    (if (nil? y) (list) y)
    (cons (first x) (concat (rest x) y))))

^#^{:doc "Returns a lazy sequence of the first n items in coll."}
(defn take [n coll]
  (if (or (<= n 0) (empty? coll))
    (list)
    (cons (first coll) (take (dec n) (rest coll)))))

^#^{:doc "Returns a lazy sequence of all but the first n items in coll."}
(defn drop [n coll]
  (if (or (<= n 0) (empty? coll))
    coll
    (drop (dec n) (rest coll))))

^#^{:doc "Returns the last item in coll, in linear time."}
(defn last [coll]
  (if (empty? coll)
    nil
    (if (empty? (rest coll))
      (first coll)
      (last (rest coll)))))

; ============================================================================
; Predicate Functions (Phase 2)
; ============================================================================

^#^{:doc "Returns the first logical true value of (pred x) for any x in coll, else nil."}
(defn some [pred coll]
  (if (empty? coll)
    nil
    (let [result (pred (first coll))]
      (if result
        result
        (some pred (rest coll))))))

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
(defn mapcat [f coll]
  (if (empty? coll)
    (list)
    (concat (f (first coll)) (mapcat f (rest coll)))))

^#^{:doc "Returns a lazy sequence of successive items from coll while (pred item) returns logical true."}
(defn take-while [pred coll]
  (if (empty? coll)
    (list)
    (if (pred (first coll))
      (cons (first coll) (take-while pred (rest coll)))
      (list))))

^#^{:doc "Returns a lazy sequence of the items in coll starting from the first item for which (pred item) returns logical false."}
(defn drop-while [pred coll]
  (if (empty? coll)
    coll
    (if (pred (first coll))
      (drop-while pred (rest coll))
      coll)))

^#^{:doc "Return a seq of all but the last item in coll, in linear time."}
(defn butlast [coll]
  (if (or (empty? coll) (empty? (rest coll)))
    nil
    (cons (first coll) (butlast (rest coll)))))

^#^{:doc "Returns a lazy sequence of the non-nil results of (f item)."}
(defn keep [f coll]
  (if (empty? coll)
    (list)
    (let [result (f (first coll))]
      (if (nil? result)
        (keep f (rest coll))
        (cons result (keep f (rest coll)))))))

^#^{:doc "Returns a lazy seq of the first item in each coll, then the second etc."}
(defn interleave [c1 c2]
  (if (or (empty? c1) (empty? c2))
    (list)
    (cons (first c1)
          (cons (first c2)
                (interleave (rest c1) (rest c2))))))

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
(defn group-by [f coll]
  (reduce (fn [ret x]
            (let [k (f x)]
              (assoc ret k (conj (get ret k []) x))))
          {} coll))

^#^{:doc "Returns a lazy sequence of the elements of coll with duplicates removed."}
(defn distinct [coll]
  (let [step (fn step [seen coll]
               (if (empty? coll)
                 (list)
                 (let [x (first coll)]
                   (if (get seen x)
                     (step seen (rest coll))
                     (cons x (step (assoc seen x true) (rest coll)))))))]
    (step {} coll)))

; ============================================================================
; Partitioning Functions (Phase 5)
; ============================================================================

^#^{:doc "Returns a lazy sequence of lists of n items each."}
(defn partition [n coll]
  (if (empty? coll)
    (list)
    (let [p (take n coll)]
      (if (< (count (vec p)) n)
        (list)
        (cons (vec p) (partition n (drop n coll)))))))

^#^{:doc "Returns a lazy sequence of lists like partition, but may include partitions with fewer than n items at the end."}
(defn partition-all [n coll]
  (if (empty? coll)
    (list)
    (cons (vec (take n coll)) (partition-all n (drop n coll)))))

^#^{:doc "Returns a vector of [(take n coll) (drop n coll)]."}
(defn split-at [n coll]
  (vector (vec (take n coll)) (vec (drop n coll))))

^#^{:doc "Returns a vector of [(take-while pred coll) (drop-while pred coll)]."}
(defn split-with [pred coll]
  (vector (vec (take-while pred coll)) (vec (drop-while pred coll))))

; ============================================================================
; Map Construction Functions (Phase 6)
; ============================================================================

^#^{:doc "Returns a map with the keys mapped to the corresponding vals."}
(defn zipmap [ks vs]
  (let [step (fn step [m ks vs]
               (if (or (empty? ks) (empty? vs))
                 m
                 (step (assoc m (first ks) (first vs))
                       (rest ks) (rest vs))))]
    (step {} ks vs)))

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
  (fn [x] (vector (f x) (g x))))

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

)CLOJURE"

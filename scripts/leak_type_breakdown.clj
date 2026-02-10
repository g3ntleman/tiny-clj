;; Type-level breakdown for heap growth investigation.
;; Run: ./build/tiny-clj-repl -f scripts/leak_type_breakdown.clj
;;
;; Notes:
;; - We keep helper definitions at the top, then run probes.
;; - A control probe (stats -> stats) is printed first to show measurement overhead.

(require 'tiny-clj.runtime)

(defn ms [stats k]
  (get (get stats :memory-stats) k))

(defn bytes-by-type [stats]
  (get (get stats :memory-stats) :bytes-by-type))

(defn rowv [row k]
  (or (get row k) 0))

(defn diff-row [a b]
  {:bytes-current (- (rowv a :bytes-current) (rowv b :bytes-current))
   :bytes-peak (- (rowv a :bytes-peak) (rowv b :bytes-peak))
   :alloc-count (- (rowv a :alloc-count) (rowv b :alloc-count))
   :dealloc-count (- (rowv a :dealloc-count) (rowv b :dealloc-count))})

(defn keys-union [m1 m2]
  (let [k1 (keys (or m1 {}))
        k2 (keys (or m2 {}))]
    (distinct (concat k1 k2))))

(defn non-zero-row? [dr]
  (or (not (= 0 (get dr :bytes-current)))
      (not (= 0 (get dr :alloc-count)))
      (not (= 0 (get dr :dealloc-count)))))

(defn diff-by-type [stats-after stats-before]
  (let [a (bytes-by-type stats-after)
        b (bytes-by-type stats-before)]
    (let [ks (keys-union a b)]
      (loop [out {} ks ks]
        (if (empty? ks)
          out
          (let [k (first ks)
                dr (diff-row (get a k) (get b k))]
            (recur
              (if (non-zero-row? dr) (assoc out k dr) out)
              (rest ks))))))))

(defn run-probe [label thunk]
  (let [s0 (tiny-clj.runtime/stats)
        _ (thunk)
        s1 (tiny-clj.runtime/stats)
        delta (- (ms s1 :bytes-current) (ms s0 :bytes-current))]
    (println "== " label " ==")
    (println "bytes-current before:" (ms s0 :bytes-current))
    (println "bytes-current after: " (ms s1 :bytes-current))
    (println "bytes-current delta: " delta)
    (println "by-type delta (non-zero only):")
    (println (diff-by-type s1 s0))
    (println)
    delta))

(println "Type-level breakdown for lineargrowth investigation")
(println)

(def control-delta
  (run-probe "control (stats -> stats)" (fn [] (tiny-clj.runtime/stats))))

(println "Control delta (measurement overhead):" control-delta)
(println)

(run-probe "workload (assoc literal)" (fn [] (assoc {:a 1 :b 2} :c 3)))
(run-probe "workload (vector literal)" (fn [] [1 2 3]))
(run-probe "workload (list literal)" (fn [] (list 1 2 3)))
(run-probe "counterprobe (assoc var)" (fn [] (do (def m {:a 1 :b 2}) (assoc m :c 3))))
(run-probe "counterprobe (discard assoc result)" (fn [] (do (assoc {:a 1 :b 2} :c 3) nil)))

;; Leak diagnosis helper:
;; - Avoids `def` of big stats maps (would retain them globally).
;; - Measures (stats->stats) overhead separately from workload.
;;
;; Run: ./build/tiny-clj-repl -f scripts/leak_diagnose.clj

(require 'tinyclj.runtime)

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

(defn diff-by-type [stats-after stats-before]
  (let [a (bytes-by-type stats-after)
        b (bytes-by-type stats-before)]
    ;; NOTE: tiny-clj's `reduce` is 2-arity only, so implement a small loop here.
    (let [ks (keys-union a b)]
      (loop [out {} ks ks]
        (if (empty? ks)
          out
          (let [k (first ks)
                dr (diff-row (get a k) (get b k))]
            (recur
              (if (and (= 0 (get dr :bytes-current))
                       (= 0 (get dr :alloc-count))
                       (= 0 (get dr :dealloc-count)))
                out
                (assoc out k dr))
              (rest ks))))))))

(defn print-diff [label stats-after stats-before]
  ;; NOTE: Function bodies are not implicitly `do` in tiny-clj;
  ;; wrap multi-step bodies in an explicit (do ...).
  (do
    (println "== " label " ==")
    (let [bc-a (ms stats-after :bytes-current)
          bc-b (ms stats-before :bytes-current)]
      (println "bytes-current before:" bc-b)
      (println "bytes-current after: " bc-a)
      (println "bytes-current delta: " (- bc-a bc-b)))
    (println "by-type delta (non-zero only):")
    (println (diff-by-type stats-after stats-before))
    (println)))

;; ---------------------------------------------------------------------------
;; 1) Control: stats -> stats (overhead of calling stats itself)
;; ---------------------------------------------------------------------------
(let [s0 (tinyclj.runtime/stats)
      s1 (tinyclj.runtime/stats)]
  (print-diff "control (stats -> stats)" s1 s0))

;; ---------------------------------------------------------------------------
;; 2) Workload: one big reduce, between two stats snapshots
;; ---------------------------------------------------------------------------
(let [s0 (tinyclj.runtime/stats)
      _ (reduce + (range 500))
      s1 (tinyclj.runtime/stats)]
  (print-diff "workload (reduce + (range 500))" s1 s0))

;; ---------------------------------------------------------------------------
;; 3) Workload: 20 separate reduces inside one top-level form
;;    (helps distinguish per-form pool effects vs retained growth)
;; ---------------------------------------------------------------------------
(let [s0 (tinyclj.runtime/stats)
      _ (do
          (reduce + (range 500)) (reduce + (range 500)) (reduce + (range 500)) (reduce + (range 500)) (reduce + (range 500))
          (reduce + (range 500)) (reduce + (range 500)) (reduce + (range 500)) (reduce + (range 500)) (reduce + (range 500))
          (reduce + (range 500)) (reduce + (range 500)) (reduce + (range 500)) (reduce + (range 500)) (reduce + (range 500))
          (reduce + (range 500)) (reduce + (range 500)) (reduce + (range 500)) (reduce + (range 500)) (reduce + (range 500)))
      s1 (tinyclj.runtime/stats)]
  (print-diff "workload (20x reduce in one form)" s1 s0))


;; Computer Language Benchmarks Game - Binary Trees
;; Original source: https://benchmarksgame-team.pages.debian.net/benchmarksgame/
;; License: BSD-3-Clause

;; Namespace: clojure.benchmarksgame.binarytrees
;; TODO: Replace with (ns clojure.benchmarksgame.binarytrees) when require/use available

;; === Benchmark Implementation ===
(defn make-tree [item depth]
  (if (zero? depth)
    [item nil nil]
    (let [i (dec item)
          d (dec depth)]
      [item (make-tree i d) (make-tree i d)])))

(defn check-tree [tree]
  (if (nil? tree)
    0
    (let [[item left right] tree]
      (+ item (check-tree left) (check-tree right)))))

(defn binary-trees [n]
  (let [min-depth 4
        max-depth (max (+ min-depth 2) n)]
    (let [stretch-depth (inc max-depth)
          stretch-tree (make-tree 0 stretch-depth)
          check (check-tree stretch-tree)]
      (println (str "stretch tree of depth " stretch-depth " check: " check)))
    
    (let [long-lived-tree (make-tree 0 max-depth)]
      (doseq [depth (range min-depth (inc max-depth) 2)]
        (let [iterations (bit-shift-left 1 (+ max-depth min-depth (- depth)))
              check (reduce + (for [_ (range iterations)]
                               (check-tree (make-tree 1 depth))))]
          (println (str (quot (* 2 iterations) 1000) "\t trees of depth " depth " check: " check))))
      
      (println (str "long lived tree of depth " max-depth " check: " (check-tree long-lived-tree))))))

;; === Benchmark Execution ===
;; Run binary trees benchmark with depth 10
(binary-trees 10)

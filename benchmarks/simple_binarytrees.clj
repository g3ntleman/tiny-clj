;; Computer Language Benchmarks Game - Simple Binary Trees
;; Simplified version for tiny-clj compatibility

^#^{:doc "Creates a binary tree represented as [item left right] of given depth."}
(defn make-tree [item depth]
  (if (zero? depth)
    [item nil nil]
    (let [i (- item 1)
          d (- depth 1)]
      [item (make-tree i d) (make-tree i d)])))

^#^{:doc "Computes the check value of a tree created by make-tree."}
(defn check-tree [tree]
  (if (nil? tree)
    0
    (let [[item left right] tree]
      (+ item (check-tree left) (check-tree right)))))

;; Simple binary trees test
(let [tree (make-tree 0 4)
      check (check-tree tree)]
  check)

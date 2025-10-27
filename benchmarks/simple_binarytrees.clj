;; Computer Language Benchmarks Game - Simple Binary Trees
;; Simplified version for tiny-clj compatibility

(defn make-tree [item depth]
  (if (zero? depth)
    [item nil nil]
    (let [i (- item 1)
          d (- depth 1)]
      [item (make-tree i d) (make-tree i d)])))

(defn check-tree [tree]
  (if (nil? tree)
    0
    (let [[item left right] tree]
      (+ item (check-tree left) (check-tree right)))))

;; Simple binary trees test
(let [tree (make-tree 0 4)
      check (check-tree tree)]
  check)

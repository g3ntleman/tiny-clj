;; Tiny-CLJ compatible Binary Trees benchmark (no let/doseq/for/destructuring)

(defn make-tree [item depth]
  (if (= depth 0)
    [item nil nil]
    [item (make-tree (- item 1) (- depth 1)) (make-tree (- item 1) (- depth 1))]))

(defn check-tree [tree]
  (if (= tree nil)
    0
    (+ (nth tree 0) (check-tree (nth tree 1)) (check-tree (nth tree 2)))))

;; power of two: 2^k
(defn pow2 [k]
  (if (<= k 0) 1 (* 2 (pow2 (- k 1)))))

;; iterate "for" end times and accumulate checks
(defn iter-check [i end depth acc]
  (if (>= i end)
    acc
    (iter-check (+ i 1) end depth (+ acc (check-tree (make-tree 1 depth))))))

;; depth loop from d to max_d step 2
(defn depth-loop [d max_d min_d long_lived]
  (if (> d max_d)
    nil
    (do
      (def iters (pow2 (+ max_d min_d (- d))))
      (def chk (iter-check 0 iters d 0))
      (println (/ (* 2 iters) 1000) " trees of depth " d " check: " chk)
      (depth-loop (+ d 2) max_d min_d long_lived))))

(defn binary-trees-tiny [n]
  (def min_depth 4)
  (def max_depth (if (> (+ min_depth 2) n) (+ min_depth 2) n))
  (def stretch_depth (+ max_depth 1))
  (def stretch_tree (make-tree 0 stretch_depth))
  (def stretch_check (check-tree stretch_tree))
  (println "stretch tree of depth " stretch_depth " check: " stretch_check)
  (def long_lived (make-tree 0 max_depth))
  (depth-loop min_depth max_depth min_depth long_lived)
  (println "long lived tree of depth " max_depth " check: " (check-tree long_lived)))

;; Benchmark entry
(time (binary-trees-tiny 10))



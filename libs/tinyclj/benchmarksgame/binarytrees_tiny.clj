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
(defn depth-loop [d max_d min_d]
  (if (> d max_d)
    nil
    (do
      (println (/ (* 2 (pow2 (+ max_d min_d (- d)))) 1000)
               " trees of depth " d " check: "
               (iter-check 0 (pow2 (+ max_d min_d (- d))) d 0))
      (depth-loop (+ d 2) max_d min_d))))

(defn binary-trees-tiny [n]
  (if (< n 6)
    (binary-trees-tiny 6) ; ensure reasonable depth
    (do
      (println "stretch tree of depth " (+ n 1)
               " check: " (check-tree (make-tree 0 (+ n 1))))
      (depth-loop 4 n 4)
      (println "long lived tree of depth " n
               " check: " (check-tree (make-tree 0 n))))))

;; Benchmark entry
(time (binary-trees-tiny 10))



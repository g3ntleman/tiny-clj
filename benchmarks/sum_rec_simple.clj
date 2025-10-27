(defn sum-rec [n]
  (if (<= n 0)
    0
    (+ n (sum-rec (- n 1)))))

(sum-rec 100)

;; Simple arithmetic benchmark
^#^{:doc "Computes the sum of squares 1^2+...+n^2 using a loop/recur."}
(defn sum-squares [n]
  (let [sum 0]
    (loop [i 1 sum sum]
      (if (<= i n)
        (recur (+ i 1) (+ sum (* i i)))
        sum))))

(sum-squares 1000)

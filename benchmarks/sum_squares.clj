;; Simple arithmetic benchmark
(defn sum-squares [n]
  (let [sum 0]
    (loop [i 1 sum sum]
      (if (<= i n)
        (recur (+ i 1) (+ sum (* i i)))
        sum))))

(sum-squares 1000)

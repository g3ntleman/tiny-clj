(defn simple [n] (if (= n 0) 0 (+ 1 (simple (- n 1)))))
(simple 3)

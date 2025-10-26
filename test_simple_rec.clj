(defn simple-rec [n] (if (= n 0) 0 (+ 1 (simple-rec (- n 1)))))
(simple-rec 3)

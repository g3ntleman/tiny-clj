;; Performance test for let bindings
(defn test-let-performance []
  (let [x 1
        y 2
        z 3
        a 4
        b 5
        c 6
        d 7
        e 8
        f 9
        g 10]
    (+ x y z a b c d e f g)))

;; Test nested let performance
(defn test-nested-let-performance []
  (let [x 1]
    (let [y 2]
      (let [z 3]
        (let [a 4]
          (let [b 5]
            (+ x y z a b)))))))

(test-let-performance)
(test-nested-let-performance)
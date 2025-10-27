;; Computer Language Benchmarks Game - Mandelbrot
;; Original source: https://benchmarksgame-team.pages.debian.net/benchmarksgame/
;; License: BSD-3-Clause

;; Namespace: clojure.benchmarksgame.mandelbrot
;; TODO: Replace with (ns clojure.benchmarksgame.mandelbrot) when require/use available

;; === Benchmark Implementation ===
(defn mandelbrot [size]
  (let [sum (atom 0)]
    (doseq [y (range size)]
      (let [ci (- (* 2.0 (/ y size)) 1.0)]
        (doseq [x (range size)]
          (let [cr (- (* 2.0 (/ x size)) 1.5)
                zr (atom 0.0)
                zi (atom 0.0)
                i (atom 0)]
            (while (and (< @i 50) 
                        (< (+ (* @zr @zr) (* @zi @zi)) 4.0))
              (let [zr' (- (* @zr @zr) (* @zi @zi) cr)
                    zi' (+ (* 2.0 @zr @zi) ci)]
                (reset! zr zr')
                (reset! zi zi')
                (swap! i inc)))
            (when (< @i 50)
              (swap! sum inc))))))
    (println (str "P4\n" size " " size))
    (println @sum)))

;; === Benchmark Execution ===
;; Run mandelbrot benchmark with size 200
(mandelbrot 200)

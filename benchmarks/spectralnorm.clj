;; Computer Language Benchmarks Game - Spectral Norm
;; Original source: https://benchmarksgame-team.pages.debian.net/benchmarksgame/
;; License: BSD-3-Clause

;; Namespace: clojure.benchmarksgame.spectralnorm
;; TODO: Replace with (ns clojure.benchmarksgame.spectralnorm) when require/use available

;; === Benchmark Implementation ===
(defn a [i j]
  (/ 1.0 (+ i j 1)))

(defn multiply-av [v av]
  (let [n (count v)]
    (loop [i 0 av' av]
      (if (< i n)
        (let [av'' (assoc av' i 
                          (loop [j 0 sum 0.0]
                            (if (< j n)
                              (recur (inc j) (+ sum (* (a i j) (nth v j))))
                              sum)))]
          (recur (inc i) av''))
        av'))))

(defn multiply-atv [v atv]
  (let [n (count v)]
    (loop [i 0 atv' atv]
      (if (< i n)
        (let [atv'' (assoc atv' i
                           (loop [j 0 sum 0.0]
                             (if (< j n)
                               (recur (inc j) (+ sum (* (a j i) (nth v j))))
                               sum)))]
          (recur (inc i) atv''))
        atv'))))

(defn spectral-norm [n]
  (let [u (vec (repeat n 1.0))
        v (vec (repeat n 0.0))]
    (loop [i 0 u' u v' v]
      (if (< i 10)
        (let [v'' (multiply-atv u' (vec (repeat n 0.0)))
              u'' (multiply-av v'' (vec (repeat n 0.0)))]
          (recur (inc i) u'' v''))
        (let [vband (Math/sqrt (reduce + (map #(* % %) (map * u' v'))))
              vbnorm (/ 1.0 vband)]
          (println (format "%.9f" (* vband vbnorm))))))))

;; === Benchmark Execution ===
;; Run spectral norm benchmark with n=100
(spectral-norm 100)

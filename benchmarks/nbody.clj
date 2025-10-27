;; Computer Language Benchmarks Game - N-Body
;; Original source: https://benchmarksgame-team.pages.debian.net/benchmarksgame/
;; License: BSD-3-Clause

;; Namespace: clojure.benchmarksgame.nbody
;; TODO: Replace with (ns clojure.benchmarksgame.nbody) when require/use available

;; === Benchmark Implementation ===
(defn advance [bodies dt]
  (let [n (count bodies)]
    (loop [i 0
           new-bodies bodies]
      (if (< i n)
        (let [body (nth new-bodies i)
              [x y z vx vy vz m] body
              new-body (loop [j 0
                              vx' vx
                              vy' vy
                              vz' vz]
                         (if (< j n)
                           (if (not= i j)
                             (let [other (nth new-bodies j)
                                   [ox oy oz _ _ _ om] other
                                   dx (- x ox)
                                   dy (- y oy)
                                   dz (- z oz)
                                   dsq (+ (* dx dx) (* dy dy) (* dz dz))
                                   mag (/ dt (* dsq (Math/sqrt dsq)))
                                   dx (* dx om mag)
                                   dy (* dy om mag)
                                   dz (* dz om mag)]
                               (recur (inc j)
                                      (- vx' dx)
                                      (- vy' dy)
                                      (- vz' dz)))
                             (recur (inc j) vx' vy' vz'))
                           [vx' vy' vz']))
              [vx' vy' vz'] new-body]
          (recur (inc i)
                 (assoc new-bodies i [x y z vx' vy' vz' m])))
        new-bodies))))

(defn energy [bodies]
  (let [n (count bodies)]
    (loop [i 0 e 0.0]
      (if (< i n)
        (let [body (nth bodies i)
              [x y z _ _ _ m] body
              e' (loop [j (inc i) e'' e]
                   (if (< j n)
                     (let [other (nth bodies j)
                           [ox oy oz _ _ _ om] other
                           dx (- x ox)
                           dy (- y oy)
                           dz (- z oz)
                           distance (Math/sqrt (+ (* dx dx) (* dy dy) (* dz dz)))]
                       (recur (inc j) (- e'' (/ (* m om) distance))))
                     e''))]
          (let [vx (nth body 3)
                vy (nth body 4)
                vz (nth body 5)
                e'' (+ e' (* 0.5 m (+ (* vx vx) (* vy vy) (* vz vz))))]
            (recur (inc i) e'')))
        e))))

(defn nbody [n]
  (let [pi 3.141592653589793
        solar-mass 4.0
        days-per-year 365.24
        dt 0.01
        
        ;; Sun
        sun [0.0 0.0 0.0 0.0 0.0 0.0 solar-mass]
        
        ;; Jupiter
        jupiter [4.84143144246472090e+00
                 -1.16032004402742839e+00
                 -1.03622044471123109e-01
                 1.66007664274403694e-03
                 7.69901118419740425e-03
                 -6.90460016972063023e-05
                 9.54791938424326609e-04]
        
        ;; Saturn
        saturn [8.34336671824457987e+00
                4.12479856412430479e+00
                -4.03523417114321381e-01
                -2.76742510726862411e-03
                4.99852801234917238e-03
                2.30417297573763929e-05
                2.85885980666130812e-04]
        
        ;; Uranus
        uranus [1.28943695621391310e+01
                -1.51111514016986312e+01
                -2.23307578892655734e-01
                2.96460137564761618e-03
                2.37847173959480950e-03
                -2.96589568540237556e-05
                4.36624404335156298e-05]
        
        ;; Neptune
        neptune [1.53796971148509165e+01
                 -2.59193146099879641e+01
                 1.79258772950371181e-01
                 2.68067772490389322e-03
                 1.62824170038242295e-03
                 -9.51592254519715870e-05
                 5.15138902046611451e-05]
        
        bodies [sun jupiter saturn uranus neptune]]
    
    ;; Offset momentum
    (let [px (reduce + (map #(* (nth % 3) (nth % 6)) bodies))
          py (reduce + (map #(* (nth % 4) (nth % 6)) bodies))
          pz (reduce + (map #(* (nth % 5) (nth % 6)) bodies))
          sun' (assoc sun 3 (/ (- px) solar-mass))
          sun'' (assoc sun' 4 (/ (- py) solar-mass))
          sun''' (assoc sun'' 5 (/ (- pz) solar-mass))
          bodies' (assoc bodies 0 sun''')]
      
      (println (format "%.9f" (energy bodies')))
      
      (loop [i 0 bodies'' bodies']
        (if (< i n)
          (recur (inc i) (advance bodies'' dt))
          (println (format "%.9f" (energy bodies''))))))))

;; === Benchmark Execution ===
;; Run n-body benchmark with 1000 iterations
(nbody 1000)

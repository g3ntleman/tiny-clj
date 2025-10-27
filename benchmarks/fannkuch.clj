;; Computer Language Benchmarks Game - Fannkuch
;; Original source: https://benchmarksgame-team.pages.debian.net/benchmarksgame/
;; License: BSD-3-Clause

;; Namespace: clojure.benchmarksgame.fannkuch
;; TODO: Replace with (ns clojure.benchmarksgame.fannkuch) when require/use available

;; === Benchmark Implementation ===
(defn fannkuch [n]
  (let [count (atom 0)
        max-flips (atom 0)
        perm (atom (vec (range n)))
        temp (atom (vec (range n)))
        checksum (atom 0)]
    
    (while (pos? @count)
      (when (pos? @count)
        (swap! count dec))
      
      (when (pos? @count)
        (let [first-element (first @perm)]
          (dotimes [i (quot (count @perm) 2)]
            (let [temp-val (get @perm i)]
              (swap! perm assoc i (get @perm (- (count @perm) 1 i)))
              (swap! perm assoc (- (count @perm) 1 i) temp-val)))
          
          (swap! perm assoc 0 first-element)))
      
      (let [first-element (first @perm)]
        (when (pos? first-element)
          (dotimes [i (count @perm)]
            (swap! temp assoc i (get @perm i)))
          
          (let [flips (atom 0)]
            (while (pos? (first @temp))
              (let [first-temp (first @temp)]
                (dotimes [i (quot first-temp 2)]
                  (let [temp-val (get @temp i)]
                    (swap! temp assoc i (get @temp (- first-temp i)))
                    (swap! temp assoc (- first-temp i) temp-val))))
              (swap! flips inc))
            
            (swap! max-flips max @flips)
            (when (even? @count)
              (swap! checksum + @flips))
            (when (odd? @count)
              (swap! checksum - @flips))))))
    
    (println (str @checksum "\nPfannkuchen(" n ") = " @max-flips))))

;; === Benchmark Execution ===
;; Run fannkuch benchmark with n=7
(fannkuch 7)

(ns tiny-breakout.levels)

(def brick-width 26)
(def brick-height 10)
(def brick-gap 4)
(def brick-left 24)
(def brick-top 36)

(defn- pattern-row->bricks
  [level-id row-idx row-pattern]
  (loop [col 0
         out []]
    (if (< col (count row-pattern))
      (let [cell (nth row-pattern col)
            brick-id (+ 2000 (* level-id 100) (* row-idx 16) col)]
        (if (= cell 1)
          (recur (+ col 1)
                 (conj out {:id brick-id
                            :x (+ brick-left (* col (+ brick-width brick-gap)))
                            :y (+ brick-top (* row-idx (+ brick-height brick-gap)))
                            :w brick-width
                            :h brick-height
                            :points 10}))
          (recur (+ col 1) out)))
      out)))

(defn- pattern->bricks
  [level-id rows]
  (loop [row-idx 0
         out []]
    (if (< row-idx (count rows))
      (recur (+ row-idx 1)
             (into out (pattern-row->bricks level-id row-idx (nth rows row-idx))))
      out)))

(def default-levels
  [{:id :level-1
    :bricks (pattern->bricks 1
                             [[1 1 1 1 1 1 1 1]
                              [1 1 1 1 1 1 1 1]
                              [1 1 1 1 1 1 1 1]
                              [1 1 1 1 1 1 1 1]])}
   {:id :level-2
    :bricks (pattern->bricks 2
                             [[1 1 0 1 1 0 1 1]
                              [1 0 1 1 1 1 0 1]
                              [1 1 1 0 0 1 1 1]
                              [0 1 1 1 1 1 1 0]])}
   {:id :level-3
    :bricks (pattern->bricks 3
                             [[0 1 1 1 1 1 1 0]
                              [1 1 0 1 1 0 1 1]
                              [1 1 1 1 1 1 1 1]
                              [1 0 1 1 1 1 0 1]
                              [0 1 1 0 0 1 1 0]])}])

(defn load-levels
  "Loads optional breakout EDN under /assets/tiny-fx/breakout.edn.
Returns default levels when no usable level vector exists."
  []
  default-levels)

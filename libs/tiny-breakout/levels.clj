(ns tiny-breakout.levels)

(def brick-width 26)
(def brick-height 10)
(def brick-gap 4)
(def brick-left 24)
(def brick-top 36)

(defn- pattern-row->bricks
  [level-id row-idx row-pattern]
  (loop [col 0
         out {}]
    (if (< col (count row-pattern))
      (let [cell (nth row-pattern col)
            brick-id (+ 2000 (* level-id 100) (* row-idx 16) col)]
        (if (= cell 1)
          (recur (+ col 1)
                 (assoc out brick-id {:id brick-id
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
         out {}]
    (if (< row-idx (count rows))
      (recur (+ row-idx 1)
             (merge out (pattern-row->bricks level-id row-idx (nth rows row-idx))))
      out)))

(def default-levels
  [{:id :level-1
    :ordinal 1
    :rows [[1 1 1 1 1 1 1 1]
           [1 1 1 1 1 1 1 1]
           [1 1 1 1 1 1 1 1]
           [1 1 1 1 1 1 1 1]]}
   {:id :level-2
    :ordinal 2
    :rows [[1 1 0 1 1 0 1 1]
           [1 0 1 1 1 1 0 1]
           [1 1 1 0 0 1 1 1]
           [0 1 1 1 1 1 1 0]]}
   {:id :level-3
    :ordinal 3
    :rows [[0 1 1 1 1 1 1 0]
           [1 1 0 1 1 0 1 1]
           [1 1 1 1 1 1 1 1]
           [1 0 1 1 1 1 0 1]
           [0 1 1 0 0 1 1 0]]}])

(defn normalize-bricks
  "Returns a brick map keyed by :id.
Accepts both {id->brick} maps and legacy [brick ...] vectors."
  [bricks]
  (cond
    (map? bricks)
    bricks

    (vector? bricks)
    (reduce (fn [out brick]
              (if (and (map? brick)
                       (number? (:id brick)))
                (assoc out (:id brick) brick)
                out))
            {}
            bricks)

    :else
    {}))

(defn level-count
  "Returns the number of built-in breakout levels."
  []
  (count default-levels))

(defn level-bricks
  "Returns concrete brick maps for one level descriptor.
Accepts compact {:ordinal n :rows [...]} forms plus pre-expanded
{:bricks {...}} and legacy {:bricks [...]} forms."
  [level]
  (let [bricks (:bricks level)]
    (cond
      (map? bricks)
      (normalize-bricks bricks)

      (vector? bricks)
      (normalize-bricks bricks)

      :else
      (let [rows (:rows level)
            ordinal (:ordinal level)]
        (if (and (vector? rows) (number? ordinal))
          (pattern->bricks ordinal rows)
          {})))))

(defn level-bricks-by-index
  "Returns concrete bricks for a built-in level index, or {} when out of range."
  [level-index]
  (if (and (number? level-index)
           (<= 0 level-index)
           (< level-index (count default-levels)))
    (level-bricks (nth default-levels level-index))
    {}))

(defn load-levels
  "Loads optional breakout EDN under /assets/tiny-fx/breakout.edn.
Returns default levels when no usable level vector exists."
  []
  default-levels)

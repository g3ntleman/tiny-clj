(ns tiny-breakout.input)

(defn- clamp
  [v min-v max-v]
  (if (< v min-v)
    min-v
    (if (> v max-v)
      max-v
      v)))

(defn normalize-paddle-intent
  "Normalizes one input map into {:dx n :launch? bool :pause? bool}.
Only deterministic scalar fields are produced."
  [input]
  (let [left? (= true (get input :left))
        right? (= true (get input :right))
        rotary (let [v (get input :rotary-delta)]
                 (if (number? v) v 0))
        digital-dx (cond
                     (and left? (not right?)) -1
                     (and right? (not left?)) 1
                     :else 0)
        dx (+ digital-dx rotary)
        launch? (= true (get input :launch))
        pause? (= true (get input :pause))]
    {:dx (clamp dx -8 8)
     :launch? launch?
     :pause? pause?}))

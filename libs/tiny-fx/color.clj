(ns tiny-fx.color)

;; Color helpers
;;
;; web-hex->color converts CSS-like #RRGGBB strings to RGB565 integer colors.
;; Returns nil for invalid input.
(defn- hex-digit-value [i]
    (cond
      (and (>= i 48) (<= i 57)) (- i 48)
      (and (>= i 97) (<= i 102)) (+ (- i 97) 10)
      (and (>= i 65) (<= i 70)) (+ (- i 65) 10)
      :else -1))

(defn- parse-hex2 [s offset]
  (let [c1 (hex-digit-value (nth s offset))
        c2 (hex-digit-value (nth s (+ offset 1)))]
    (if (or (< c1 0) (< c2 0))
      nil
      (+ (* c1 16) c2))))

(defn color
  "Converts either a 24-bit RGB888 integer (0xRRGGBB) or three 8-bit RGB
channels to an RGB565 integer color. Returns nil for invalid input."
  [& args]
  :native)

(defn web-hex->color
  "Converts #RRGGBB into RGB565 integer color. Returns nil for invalid input."
  [s]
  (if (nil? s)
    nil
    (let [t (str s)]
      (if (and (= (count t) 7) (= (subs t 0 1) "#"))
        (let [r (parse-hex2 t 1)
              g (parse-hex2 t 3)
              b (parse-hex2 t 5)]
          (if (or (nil? r) (nil? g) (nil? b))
            nil
            (color r g b)))
        nil))))

(defn step-keyframes
  "Turns monotonic [offset-ms value] stops into hold-then-step keyframes.
With one arity, returns relative keyframes. With two arities, shifts all
keyframes by start-ms."
  [& args]
  (let [argc (count args)]
    (cond
      (= argc 1)
      (let [stops (first args)]
        (if (empty? stops)
          []
          (loop [remaining (rest stops)
                 prev (first stops)
                 keyframes [(first stops)]]
            (if (empty? remaining)
              keyframes
              (let [[next-ms next-value :as next-stop] (first remaining)
                    [_ prev-value] prev]
                (recur (rest remaining)
                       next-stop
                       (conj keyframes
                             [next-ms prev-value]
                             [next-ms next-value])))))))

      (= argc 2)
      (let [start-ms (first args)
            stops (second args)]
        (mapv (fn [[offset-ms value]]
                [(+ start-ms offset-ms) value])
              (step-keyframes stops)))

      :else
      (throw "tiny-fx.color/step-keyframes expects 1 or 2 arguments"))))

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

(defn rgb888->color
  "Converts 8-bit RGB channels to RGB565 integer color."
  [r g b]
  (let [r5 (quot (* r 31) 255)
        g6 (quot (* g 63) 255)
        b5 (quot (* b 31) 255)]
    (+ (* r5 2048) (* g6 32) b5)))

(defn color
  "Converts a 24-bit RGB888 integer (0xRRGGBB) to RGB565 integer color.
Returns nil for invalid input."
  [rgb]
  (if (or (nil? rgb)
          (not (integer? rgb))
          (< rgb 0)
          (> rgb 16777215))
    nil
    (let [r (bit-and (bit-shift-right rgb 16) 255)
          g (bit-and (bit-shift-right rgb 8) 255)
          b (bit-and rgb 255)]
      (rgb888->color r g b))))

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
            (color (+ (* r 65536) (* g 256) b))))
        nil))))

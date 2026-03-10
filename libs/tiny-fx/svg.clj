(ns tiny-fx.svg
  (:require [clojure.string :as str]
            [tiny-fx.gfx-scene :as scene :refer [->Group ->Line ->Rect ->Polyline ->Style ->VText]]))

;; tiny-fx.svg
;;
;; Consolidated conversion helpers for tiny-fx:
;; - SVG -> draw-record conversion

;; ---------------------------------------------------------------------------
;; SVG -> tiny-clj draw-record importer (MVP subset)
;; ---------------------------------------------------------------------------

(defn- ws-char?
  [c]
  (or (= c \space) (= c \tab) (= c \newline) (= c \return)
      (= c 32) (= c 9) (= c 10) (= c 13)))

(defn- skip-ws
  [s i]
  (loop [idx i]
    (if (and (< idx (count s)) (ws-char? (nth s idx)))
      (recur (+ idx 1))
      idx)))

(defn- first-ws-index
  [s]
  (loop [i 0]
    (if (>= i (count s))
      nil
      (if (ws-char? (nth s i))
        i
        (recur (+ i 1))))))

(defn- parse-attrs
  [attrs-str]
  (loop [idx 0
         out {}]
    (let [n (count attrs-str)
          i (skip-ws attrs-str idx)]
      (if (>= i n)
        out
        (let [eq (str/index-of attrs-str "=" i)]
          (if (nil? eq)
            out
            (let [k (str/trim (subs attrs-str i eq))
                  vstart0 (skip-ws attrs-str (+ eq 1))]
              (if (>= vstart0 n)
                out
                (let [q (subs attrs-str vstart0 (+ vstart0 1))]
                  (if (or (= q "\"") (= q "'"))
                    (let [vstart (+ vstart0 1)
                          vend (str/index-of attrs-str q vstart)]
                      (if (nil? vend)
                        out
                        (recur (+ vend 1) (assoc out k (subs attrs-str vstart vend)))))
                    (let [next-ws (first-ws-index (subs attrs-str vstart0))
                          vend (if (nil? next-ws) n (+ vstart0 next-ws))]
                      (recur vend (assoc out k (subs attrs-str vstart0 vend))))))))))))))

(defn- parse-style-inline
  [style-str]
  (if (or (nil? style-str) (= (str/trim style-str) ""))
    {}
    (loop [parts (str/split style-str ";" nil)
           out {}]
      (if (empty? parts)
        out
        (let [chunk (str/trim (first parts))]
          (if (or (= chunk "") (nil? (str/index-of chunk ":" 0)))
            (recur (rest parts) out)
            (let [sep (str/index-of chunk ":" 0)
                  k (str/trim (subs chunk 0 sep))
                  v (str/trim (subs chunk (+ sep 1)))]
              (recur (rest parts) (assoc out k v)))))))))

(defn- attr-get
  [attrs style-map k]
  (let [v (get attrs k)]
    (if (nil? v) (get style-map k) v)))

(defn- truncate-decimal-str
  [s]
  (let [t (str/trim s)
        dot (str/index-of t "." 0)]
    (if (nil? dot) t (subs t 0 dot))))

(defn- digit-char?
  [ch]
  (or (= ch \0) (= ch 48)
      (= ch \1) (= ch 49)
      (= ch \2) (= ch 50)
      (= ch \3) (= ch 51)
      (= ch \4) (= ch 52)
      (= ch \5) (= ch 53)
      (= ch \6) (= ch 54)
      (= ch \7) (= ch 55)
      (= ch \8) (= ch 56)
      (= ch \9) (= ch 57)))

(defn- numeric-prefix
  [s]
  (let [t (str/trim (or s ""))]
    (loop [i 0
           seen-digit false
           seen-dot false]
      (if (>= i (count t))
        (if seen-digit (subs t 0 i) nil)
        (let [ch (nth t i)]
          (cond
            (and (= i 0) (or (= ch \+) (= ch 43)
                             (= ch \-) (= ch 45)))
            (recur (+ i 1) seen-digit seen-dot)

            (digit-char? ch)
            (recur (+ i 1) true seen-dot)

            (and (or (= ch \.) (= ch 46)) (not seen-dot))
            (recur (+ i 1) seen-digit true)

            :else
            (if seen-digit (subs t 0 i) nil)))))))

(defn- parse-number-default
  [s default-value]
  (let [token (numeric-prefix s)]
    (if (or (nil? token) (= token "") (= token "-") (= token "+") (= token "."))
      default-value
      (read-string token))))

(defn- parse-int-default
  [s default-value]
  (if (or (nil? s) (= (str/trim s) ""))
    default-value
    (let [token (truncate-decimal-str (or (numeric-prefix s) ""))]
      (if (or (= token "") (= token "-") (= token "+"))
        default-value
        (read-string token)))))

(defn- hex-digit-value
  [c]
  (cond
    (or (= c \0) (= c 48)) 0
    (or (= c \1) (= c 49)) 1
    (or (= c \2) (= c 50)) 2
    (or (= c \3) (= c 51)) 3
    (or (= c \4) (= c 52)) 4
    (or (= c \5) (= c 53)) 5
    (or (= c \6) (= c 54)) 6
    (or (= c \7) (= c 55)) 7
    (or (= c \8) (= c 56)) 8
    (or (= c \9) (= c 57)) 9
    (or (= c \a) (= c 97)) 10
    (or (= c \b) (= c 98)) 11
    (or (= c \c) (= c 99)) 12
    (or (= c \d) (= c 100)) 13
    (or (= c \e) (= c 101)) 14
    (or (= c \f) (= c 102)) 15
    (or (= c \A) (= c 65)) 10
    (or (= c \B) (= c 66)) 11
    (or (= c \C) (= c 67)) 12
    (or (= c \D) (= c 68)) 13
    (or (= c \E) (= c 69)) 14
    (or (= c \F) (= c 70)) 15
    :else -1))

(defn- parse-hex2
  [s offset]
  (let [c1 (hex-digit-value (nth s offset))
        c2 (hex-digit-value (nth s (+ offset 1)))]
    (if (or (< c1 0) (< c2 0))
      nil
      (+ (* c1 16) c2))))

(defn- rgb888->rgb565
  [r g b]
  (let [r5 (quot (* r 31) 255)
        g6 (quot (* g 63) 255)
        b5 (quot (* b 31) 255)]
    (+ (* r5 2048) (* g6 32) b5)))

(defn- parse-color-rgb565
  [s]
  (if (nil? s)
    nil
    (let [t (str/lower-case (str/trim s))]
      (cond
        (or (= t "") (= t "none")) nil
        (and (= (count t) 7) (= (subs t 0 1) "#"))
        (let [r (parse-hex2 t 1)
              g (parse-hex2 t 3)
              b (parse-hex2 t 5)]
          (if (or (nil? r) (nil? g) (nil? b))
            nil
            (rgb888->rgb565 r g b)))
        :else nil))))

(defn- style-from-svg
  [attrs style-map]
  (let [stroke (parse-color-rgb565 (attr-get attrs style-map "stroke"))
        fill (parse-color-rgb565 (attr-get attrs style-map "fill"))
        stroke-rgb565 (if (nil? stroke) 65535 stroke)
        has-fill (not (nil? fill))
        fill-rgb565 (if has-fill fill 0)
        stroke-width (max 1 (parse-int-default (attr-get attrs style-map "stroke-width") 1))
        display (str/lower-case (str/trim (or (attr-get attrs style-map "display") "")))
        visibility (str/lower-case (str/trim (or (attr-get attrs style-map "visibility") "")))
        visible (not (or (= display "none") (= visibility "hidden")))]
    (->Style stroke-rgb565 stroke-width visible has-fill fill-rgb565 false 0)))

(defn- style-for-text-from-svg
  [attrs style-map]
  (let [base-style (style-from-svg attrs style-map)
        fill (parse-color-rgb565 (attr-get attrs style-map "fill"))
        stroke (parse-color-rgb565 (attr-get attrs style-map "stroke"))
        text-color (if (nil? fill)
                     (if (nil? stroke) (get base-style :stroke_color) stroke)
                     fill)]
    (->Style text-color
             (get base-style :stroke_width)
             (get base-style :visible)
             false
             0
             false
             0)))

(defn- decode-xml-entities
  [s]
  (if (nil? s)
    ""
    (-> s
        (str/replace "&lt;" "<")
        (str/replace "&gt;" ">")
        (str/replace "&quot;" "\"")
        (str/replace "&apos;" "'")
        (str/replace "&amp;" "&"))))

(defn- normalize-text-content
  [s]
  (let [decoded (decode-xml-entities s)]
    (loop [current decoded]
      (let [next (-> current
                     (str/replace "\n" " ")
                     (str/replace "\t" " ")
                     (str/replace "  " " "))]
        (if (= next current)
          (str/trim next)
          (recur next))))))

(defn- parse-text-scale
  [attrs style-map]
  (let [font-size (attr-get attrs style-map "font-size")]
    (if (or (nil? font-size) (= (str/trim font-size) ""))
      1
      (/ (parse-number-default font-size 8) 8))))

(defn- parse-points
  [points-str]
  (let [normalized (-> points-str
                       (str/trim)
                       (str/replace "\n" " ")
                       (str/replace "\t" " ")
                       (str/replace "  " " "))
        tokens (str/split normalized " " nil)]
    (loop [xs tokens
           out []]
      (if (empty? xs)
        out
        (let [token (str/trim (first xs))]
          (if (= token "")
            (recur (rest xs) out)
            (let [xy (str/split token "," nil)]
              (if (< (count xy) 2)
                (recur (rest xs) out)
                (recur (rest xs)
                       (conj out [(parse-int-default (first xy) 0)
                                  (parse-int-default (nth xy 1) 0)]))))))))))

(defn- circle->polygon-points
  [attrs]
  (let [cx (parse-int-default (get attrs "cx") 0)
        cy (parse-int-default (get attrs "cy") 0)
        r (max 0 (parse-int-default (get attrs "r") 0))
        d (quot (* r 181) 256)
        outer (quot (* r 237) 256)
        inner (quot (* r 98) 256)]
    [[cx (- cy r)]
     [(+ cx inner) (- cy outer)]
     [(+ cx d) (- cy d)]
     [(+ cx outer) (- cy inner)]
     [(+ cx r) cy]
     [(+ cx outer) (+ cy inner)]
     [(+ cx d) (+ cy d)]
     [(+ cx inner) (+ cy outer)]
     [cx (+ cy r)]
     [(- cx inner) (+ cy outer)]
     [(- cx d) (+ cy d)]
     [(- cx outer) (+ cy inner)]
     [(- cx r) cy]
     [(- cx outer) (- cy inner)]
     [(- cx d) (- cy d)]
     [(- cx inner) (- cy outer)]]))

(defn- node-from-tag
  [tag-name attrs style-map node-id text-content]
  (let [style (style-from-svg attrs style-map)
        visible (get style :visible)]
    (cond
      (= tag-name "line")
      (->Line node-id
              nil
              style
              visible
              (parse-int-default (get attrs "x1") 0)
              (parse-int-default (get attrs "y1") 0)
              (parse-int-default (get attrs "x2") 0)
              (parse-int-default (get attrs "y2") 0))

      (= tag-name "rect")
      (->Rect node-id
              nil
              style
              visible
              (parse-int-default (get attrs "x") 0)
              (parse-int-default (get attrs "y") 0)
              (parse-int-default (get attrs "width") (parse-int-default (get attrs "w") 0))
              (parse-int-default (get attrs "height") (parse-int-default (get attrs "h") 0)))

      (= tag-name "polyline")
      (->Polyline node-id
                  nil
                  style
                  visible
                  (parse-points (or (get attrs "points") ""))
                  false)

      (= tag-name "polygon")
      (->Polyline node-id
                  nil
                  style
                  visible
                  (parse-points (or (get attrs "points") ""))
                  true)

      (= tag-name "circle")
      (->Polyline node-id
                  nil
                  style
                  visible
                  (circle->polygon-points attrs)
                  true)

      (= tag-name "text")
      (->VText node-id
               nil
               (style-for-text-from-svg attrs style-map)
               visible
               (parse-int-default (get attrs "x") 0)
               (parse-int-default (get attrs "y") 0)
               (parse-text-scale attrs style-map)
               0
               (normalize-text-content text-content))

      :else nil)))

(defn- parse-tag-body
  [raw-tag node-id text-content]
  (let [trimmed0 (str/trim raw-tag)
        trimmed (if (str/ends-with? trimmed0 "/")
                  (str/trim (subs trimmed0 0 (- (count trimmed0) 1)))
                  trimmed0)]
    (if (or (= trimmed "") (str/starts-with? trimmed "/"))
      [nil node-id]
      (let [split-pos (first-ws-index trimmed)
            tag-name (str/lower-case (if (nil? split-pos) trimmed (subs trimmed 0 split-pos)))
            attrs-str (if (nil? split-pos) "" (subs trimmed (+ split-pos 1)))
            attrs (parse-attrs attrs-str)
            style-map (parse-style-inline (get attrs "style"))
            node (node-from-tag tag-name attrs style-map node-id text-content)]
        (if (nil? node)
          [nil node-id]
          [node (+ node-id 1)])))))

^#^{:doc "Parses an SVG string into a Group record for tiny-clj vector rendering.

MVP behavior:
- Supports line/rect/polyline/polygon/circle/text tags.
- Produces Group/Line/Rect/Polyline/Style records with keyword fields used by C renderer.
- Coordinates are integer-based (decimal values are truncated)."}
(defn group-from-svg
  [svg-str]
  (if (or (nil? svg-str) (= (str/trim svg-str) ""))
    (->Group 1 nil nil true [])
    (loop [idx 0
           next-id 100
           nodes []]
      (let [lt (str/index-of svg-str "<" idx)]
        (if (nil? lt)
          (->Group 1 nil nil true nodes)
          (let [gt (str/index-of svg-str ">" (+ lt 1))]
            (if (nil? gt)
              (->Group 1 nil nil true nodes)
              (let [tag (subs svg-str (+ lt 1) gt)
                    trimmed-tag (str/trim tag)
                    closing-tag? (str/starts-with? trimmed-tag "/")
                    text-tag? (and (not closing-tag?)
                                   (or (= trimmed-tag "text")
                                       (str/starts-with? trimmed-tag "text ")))
                    self-closing-text? (and text-tag? (str/ends-with? trimmed-tag "/"))
                    text-close (if (and text-tag? (not self-closing-text?))
                                 (str/index-of svg-str "</text>" (+ gt 1))
                                 nil)
                    text-content (if (nil? text-close)
                                   nil
                                   (subs svg-str (+ gt 1) text-close))
                    parsed (parse-tag-body tag next-id text-content)
                    node (first parsed)
                    id2 (nth parsed 1)
                    next-idx (if (nil? text-close) (+ gt 1) (+ text-close 7))]
                (if (nil? node)
                  (recur next-idx next-id nodes)
                  (recur next-idx id2 (conj nodes node)))))))))))

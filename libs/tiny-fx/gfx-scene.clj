R"TINY_GFX_SCENE(
(ns tiny-fx.gfx-scene)

;; Central scene-record schema for tiny-fx/tiny-clj vector rendering.
;;
;; defrecord registers each type with the C renderer (which matches record
;; type names by exact unqualified symbol cname) and creates ->Type /
;; map->Type constructors that other namespaces can import via :refer.
;;
;; Record contracts consumed by the C renderer:
;; - Transform [tx ty sx sy rot]
;;   tx/ty: translation in px, sx/sy: scale factors, rot: degrees.
;; - Style [stroke_color stroke_width visible has_fill fill_color has_bg_color bg_color]
;;   colors are RGB565 integers.
;; - Group/Line/Polyline/Rect/Tri/VText all share [id t style visible ...].
;;   :id must be stable across snapshot publishes for deterministic state/collision routing.
;;   :t/:style fields can hold plain values or Timeline records.
;; - Timeline [keyframes loop]
;;   keyframes are [[time-ms value] ...] with monotonic time-ms.
;; - Scene [root clip-rect erase-color collision-rules]
;; - FrameScene [root clip-rect z visible opaque erase-color guard-px collision-rules]
;;   root may be flat entity map ({id -> Record}) or legacy nested root node.
;;   clip-rect is [x y w h], guard-px expands dirty area for slot rerender diffing.
;;   collision-rules carries host/runtime spatial trigger declarations for the published snapshot.
;; - CollisionRule / CollisionEvent are legacy names kept for compatibility.
;; - SpatialRule [id slot kind a-id b-id radius channel]
;; - Aabb [min-x min-y max-x max-y]
;; - SpatialEvent [source rule-id slot kind phase snapshot-gen a b a-aabb b-aabb radius channel]

(defrecord Transform [tx ty sx sy rot])
(defrecord Style [stroke_color stroke_width visible has_fill fill_color has_bg_color bg_color])
(defrecord Group [id t style visible children])
(defrecord Line [id t style visible x1 y1 x2 y2])
(defrecord Polyline [id t style visible pts closed])
(defrecord Rect [id t style visible x y w h])
(defrecord Tri [id t style visible x1 y1 x2 y2 x3 y3])
(defrecord VText [id t style visible x y scale rot text])
(defrecord Timeline [keyframes loop])
(defrecord Scene [root clip-rect erase-color collision-rules])
(defrecord FrameScene [root clip-rect z visible opaque erase-color guard-px collision-rules])
(defrecord CollisionRule [id slot a-id b-id phase-mask enabled cooldown-ms])
(defrecord CollisionEvent [rule-id slot a-id b-id phase snapshot-gen ts-ms])
(defrecord SpatialRule [id slot kind a-id b-id radius channel])
(defrecord Aabb [min-x min-y max-x max-y])
(defrecord SpatialEvent [source rule-id slot kind phase snapshot-gen a b a-aabb b-aabb radius channel])

;; Color helpers
;;
;; web-hex->color converts CSS-like #RRGGBB strings to RGB565 integer colors.
;; Returns nil for invalid input.
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

;; Collision/spatial contract helpers.
;; CollisionRule remains a legacy schema bridge.
;; SpatialRule/SpatialEvent define the active host-side trigger contract.
(def default-collision-slot :game)
(def default-collision-phase-mask [:enter :exit])
(def default-spatial-kind :collision)

(defn- phase-present?
  [xs v]
  (loop [rest-xs xs]
    (if (empty? rest-xs)
      false
      (if (= (first rest-xs) v)
        true
        (recur (rest rest-xs))))))

(defn normalize-collision-phase-mask
  "Normalizes phase mask for collision rules.
Accepts vector/list/keyword/nil and returns a validated vector."
  [phase-mask]
  (let [raw (cond
              (nil? phase-mask) default-collision-phase-mask
              (vector? phase-mask) phase-mask
              (list? phase-mask) (vec phase-mask)
              (keyword? phase-mask) [phase-mask]
              :else [])
        normalized (let [out []
                         out (if (phase-present? raw :enter) (conj out :enter) out)
                         out (if (phase-present? raw :exit) (conj out :exit) out)]
                     out)]
    (if (empty? normalized)
      default-collision-phase-mask
      normalized)))

(defn normalize-collision-rule
  "Applies legacy collision-rule defaults and normalization."
  [rule]
  {:id (get rule :id)
   :slot (or (get rule :slot) default-collision-slot)
   :a-id (get rule :a-id)
   :b-id (get rule :b-id)
   :phase-mask (normalize-collision-phase-mask (get rule :phase-mask))
   :enabled (if (nil? (get rule :enabled)) true (not= (get rule :enabled) false))
   :cooldown-ms (let [v (get rule :cooldown-ms)]
                  (if (nil? v) 0 v))})

(defn normalize-spatial-rule
  "Applies spatial-rule defaults for `:collision` / `:proximity` trigger wiring.
The runtime emits only edge transitions (`:enter` / `:exit`)."
  [rule]
  {:id (get rule :id)
   :slot (or (get rule :slot) default-collision-slot)
   :kind (or (get rule :kind) default-spatial-kind)
   :a-id (get rule :a-id)
   :b-id (get rule :b-id)
   :radius (let [v (get rule :radius)]
             (if (nil? v) 0 v))
   :channel (get rule :channel)})

(defn update-nodes
  "Batched scene-tree update. Takes a root node and a map of
   {:id update-fn ...}. Performs a single recursive walk, applying
   each update-fn to the node with matching :id. Once all updates
   have been applied, remaining subtrees are returned unchanged."
  [node updates]
  (if (empty? updates)
    node
    (let [id       (:id node)
          upd-fn   (when id (get updates id))
          node2    (if upd-fn (upd-fn node) node)
          updates2 (if upd-fn (dissoc updates id) updates)
          children (:children node2)]
      (if children
        (assoc node2 :children
          (mapv (fn [ch] (update-nodes ch updates2)) children))
        node2))))


)TINY_GFX_SCENE"

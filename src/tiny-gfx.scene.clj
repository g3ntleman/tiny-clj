R"TINY_GFX_SCENE(
(ns tiny-gfx.scene)

;; Central scene-record schema for tiny-gfx/tiny-clj vector rendering.
;;
;; We keep unqualified type symbols (e.g. 'Group) because the C renderer
;; matches record type names by exact symbol cname ("Group", "Line", ...).

(def transform-type 'Transform)
(def style-type 'Style)
(def group-type 'Group)
(def line-type 'Line)
(def polyline-type 'Polyline)
(def rect-type 'Rect)
(def tri-type 'Tri)
(def text-type 'VText)
(def scene-type 'Scene)
(def frame-scene-type 'FrameScene)
(def collision-rule-type 'CollisionRule)
(def collision-event-type 'CollisionEvent)

(def transform-fields [:tx :ty :sx :sy :rot])
(def style-fields [:stroke_rgb565 :stroke_width :visible :has_fill :fill_rgb565 :has_bg_rgb565 :bg_rgb565])
(def group-fields [:id :t :style :visible :children])
(def line-fields [:id :t :style :visible :x1 :y1 :x2 :y2])
(def polyline-fields [:id :t :style :visible :pts :closed])
(def rect-fields [:id :t :style :visible :x :y :w :h])
(def tri-fields [:id :t :style :visible :x1 :y1 :x2 :y2 :x3 :y3])
(def text-fields [:id :t :style :visible :x :y :scale :rot :text])
(def scene-fields [:root :clip-rect :erase-rgb565 :collision-rules])
(def frame-scene-fields [:root :clip-rect :z :visible :opaque :erase-rgb565 :guard-px :collision-rules])
(def collision-rule-fields [:id :slot :a-id :b-id :phase-mask :enabled :cooldown-ms])
(def collision-event-fields [:rule-id :slot :a-id :b-id :phase :snapshot-gen :ts-ms])

;; Collision contract helpers (Step 1: contract freeze, no runtime wiring yet).
(def default-collision-slot :game)
(def default-collision-phase-mask [:enter :exit])

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
                         out (if (phase-present? raw :stay) (conj out :stay) out)
                         out (if (phase-present? raw :exit) (conj out :exit) out)]
                     out)]
    (if (empty? normalized)
      default-collision-phase-mask
      normalized)))

(defn normalize-collision-rule
  "Applies collision-rule defaults and normalization (schema-level only)."
  [rule]
  {:id (get rule :id)
   :slot (or (get rule :slot) default-collision-slot)
   :a-id (get rule :a-id)
   :b-id (get rule :b-id)
   :phase-mask (normalize-collision-phase-mask (get rule :phase-mask))
   :enabled (if (nil? (get rule :enabled)) true (not= (get rule :enabled) false))
   :cooldown-ms (let [v (get rule :cooldown-ms)]
                  (if (nil? v) 0 v))})

(defn ensure-scene-records!
  "Register/reuse all vector scene record descriptors.
Returns nil and is idempotent."
  []
  (record-register transform-type transform-fields)
  (record-register style-type style-fields)
  (record-register group-type group-fields)
  (record-register line-type line-fields)
  (record-register polyline-type polyline-fields)
  (record-register rect-type rect-fields)
  (record-register tri-type tri-fields)
  (record-register text-type text-fields)
  (record-register scene-type scene-fields)
  (record-register frame-scene-type frame-scene-fields)
  (record-register collision-rule-type collision-rule-fields)
  (record-register collision-event-type collision-event-fields)
  nil)

;; Register eagerly on require so other namespaces can immediately create records.
(ensure-scene-records!)

)TINY_GFX_SCENE"

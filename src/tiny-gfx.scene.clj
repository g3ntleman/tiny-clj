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

(def transform-fields [:tx :ty :sx :sy :rot])
(def style-fields [:stroke_rgb565 :stroke_width :visible :has_bg_rgb565 :bg_rgb565])
(def group-fields [:id :t :style :visible :children])
(def line-fields [:id :t :style :visible :x1 :y1 :x2 :y2])
(def polyline-fields [:id :t :style :visible :pts :closed])
(def rect-fields [:id :t :style :visible :x :y :w :h])
(def tri-fields [:id :t :style :visible :x1 :y1 :x2 :y2 :x3 :y3])
(def text-fields [:id :t :style :visible :x :y :scale :rot :text])
(def scene-fields [:root :clip-rect :erase-rgb565])
(def frame-scene-fields [:root :clip-rect :z :visible :opaque :erase-rgb565 :guard-px])

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
  nil)

;; Register eagerly on require so other namespaces can immediately create records.
(ensure-scene-records!)

)TINY_GFX_SCENE"

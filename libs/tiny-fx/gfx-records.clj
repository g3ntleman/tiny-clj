(ns tiny-fx.gfx-records)

;; Canonical record schema used by both C runtime and Clojure code.
(defrecord Transform [tx ty sx sy rot])
(defrecord Style [stroke-color stroke-width visible has-fill fill-color has-bg-color bg-color])
(defrecord Group [id t style visible children prototype])
(defrecord Line [id t style visible x1 y1 x2 y2 prototype])
(defrecord Polyline [id t style visible pts closed prototype])
(defrecord Rect [id t style visible x y w h prototype])
(defrecord Tri [id t style visible x1 y1 x2 y2 x3 y3 prototype])
(defrecord VText [id t style visible x y scale rot text prototype])
(defrecord Timeline [keyframes loop end-event])
(defrecord Scene [root index clip-rect erase-color collision-rules])
(defrecord FrameScene [root index clip-rect z visible opaque erase-color guard-px collision-rules])
(defrecord CollisionRule [id slot a-id b-id phase-mask enabled cooldown-ms])
(defrecord CollisionEvent [rule-id slot a-id b-id phase snapshot-gen ts-ms])
(defrecord SpatialRule [id slot kind self other radius channel])
(defrecord Aabb [min-x min-y max-x max-y])
(defrecord SpatialEvent
  [source id slot-id kind phase self other self-entity other-entity rule
   snapshot-gen self-aabb other-aabb self-prototype other-prototype radius channel])

R"TINY_GFX_RUNTIME(
(ns tiny-fx.gfx
  (:require [tiny-clj.runtime]
            [tiny-fx.gfx-scene]
            [tiny-fx.gfx-collision]
            [tiny-fx.scene-demo]))

;; Direct var aliases to tiny-clj.runtime (no forwarding wrapper functions).
;; This keeps arity/error behavior identical to the native runtime entry points.

^#^{:doc "Benchmarks vector-scene decode+render path and returns a metrics map.
Usage: (vector-scene-bench) or (vector-scene-bench iterations warmup)."}
(def vector-scene-bench tiny-clj.runtime/vector-scene-bench)

;; Re-export the compact scene/collision API from the internal helper namespaces.
^#^{:doc "Record constructor alias for tiny-fx.gfx-scene/->Transform.
Fields: [tx ty sx sy rot]."}
(def ->Transform tiny-fx.gfx-scene/->Transform)
^#^{:doc "Record constructor alias for tiny-fx.gfx-scene/->Style.
Fields: [stroke_color stroke_width visible has_fill fill_color has_bg_color bg_color]."}
(def ->Style tiny-fx.gfx-scene/->Style)
^#^{:doc "Record constructor alias for tiny-fx.gfx-scene/->Group.
Fields: [id t style visible children]. Children are stable child ids in flat entity maps."}
(def ->Group tiny-fx.gfx-scene/->Group)
^#^{:doc "Record constructor alias for tiny-fx.gfx-scene/->Line.
Fields: [id t style visible x1 y1 x2 y2]."}
(def ->Line tiny-fx.gfx-scene/->Line)
^#^{:doc "Record constructor alias for tiny-fx.gfx-scene/->Polyline.
Fields: [id t style visible pts closed]."}
(def ->Polyline tiny-fx.gfx-scene/->Polyline)
^#^{:doc "Record constructor alias for tiny-fx.gfx-scene/->Rect.
Fields: [id t style visible x y w h]."}
(def ->Rect tiny-fx.gfx-scene/->Rect)
^#^{:doc "Record constructor alias for tiny-fx.gfx-scene/->Tri.
Fields: [id t style visible x1 y1 x2 y2 x3 y3]."}
(def ->Tri tiny-fx.gfx-scene/->Tri)
^#^{:doc "Record constructor alias for tiny-fx.gfx-scene/->VText.
Fields: [id t style visible x y scale rot text]."}
(def ->VText tiny-fx.gfx-scene/->VText)
^#^{:doc "Record constructor alias for tiny-fx.gfx-scene/->Timeline.
Fields: [keyframes loop]. Keyframes are [[time-ms value] ...]."}
(def ->Timeline tiny-fx.gfx-scene/->Timeline)
^#^{:doc "Record constructor alias for tiny-fx.gfx-scene/->Scene.
Fields: [root clip-rect erase-color collision-rules]."}
(def ->Scene tiny-fx.gfx-scene/->Scene)
^#^{:doc "Record constructor alias for tiny-fx.gfx-scene/->FrameScene.
Fields: [root clip-rect z visible opaque erase-color guard-px collision-rules]."}
(def ->FrameScene tiny-fx.gfx-scene/->FrameScene)
^#^{:doc "Legacy compatibility constructor alias for tiny-fx.gfx-scene/->CollisionRule."}
(def ->CollisionRule tiny-fx.gfx-scene/->CollisionRule)
^#^{:doc "Legacy compatibility constructor alias for tiny-fx.gfx-scene/->CollisionEvent."}
(def ->CollisionEvent tiny-fx.gfx-scene/->CollisionEvent)
^#^{:doc "Record constructor alias for tiny-fx.gfx-scene/->SpatialRule.
Fields: [id slot kind a-id b-id radius channel]."}
(def ->SpatialRule tiny-fx.gfx-scene/->SpatialRule)
^#^{:doc "Record constructor alias for tiny-fx.gfx-scene/->Aabb.
Fields: [min-x min-y max-x max-y]."}
(def ->Aabb tiny-fx.gfx-scene/->Aabb)
^#^{:doc "Record constructor alias for tiny-fx.gfx-scene/->SpatialEvent.
Fields: [source rule-id slot kind phase snapshot-gen a b a-aabb b-aabb radius channel]."}
(def ->SpatialEvent tiny-fx.gfx-scene/->SpatialEvent)
^#^{:doc "Converts a 24-bit RGB888 integer (0xRRGGBB) to RGB565."}
(def color tiny-fx.gfx-scene/color)
^#^{:doc "Converts separate 8-bit RGB channels to RGB565."}
(def rgb888->color tiny-fx.gfx-scene/rgb888->color)
^#^{:doc "Converts a CSS-style #RRGGBB string to RGB565. Returns nil for invalid input."}
(def web-hex->color tiny-fx.gfx-scene/web-hex->color)
^#^{:doc "Normalizes collision phase masks to the supported [:enter :exit] vector subset."}
(def normalize-collision-phase-mask tiny-fx.gfx-scene/normalize-collision-phase-mask)
^#^{:doc "Applies defaults to the legacy collision-rule contract."}
(def normalize-collision-rule tiny-fx.gfx-scene/normalize-collision-rule)
^#^{:doc "Applies defaults to the active spatial-rule contract used by the runtime."}
(def normalize-spatial-rule tiny-fx.gfx-scene/normalize-spatial-rule)
^#^{:doc "Convenience tree-update helper kept for compatibility.
Flat entity-map scenes should usually update nodes directly via assoc-in/swap!."}
(def update-nodes tiny-fx.gfx-scene/update-nodes)
^#^{:doc "Configures the Clojure collision/spatial callback closure used by native dispatch."}
(def set-collision-callback! tiny-fx.gfx-collision/set-collision-callback!)
^#^{:doc "Invokes the configured collision/spatial callback for one event map.
Returns nil when no callback is configured."}
(def invoke-collision-callback! tiny-fx.gfx-collision/invoke-collision-callback!)
^#^{:doc "Returns the default player-vs-obstacle collision response policy function used by the demo."}
(def player-vs-obstacle-policy tiny-fx.gfx-collision/player-vs-obstacle-policy)

^#^{:doc "Returns the canonical ordered slot descriptor vector used by the runtime
and host-viewer. Each slot descriptor contains at least `:id` and `:atom`.
Usage: (slot-descriptors)."}
(def slot-descriptors tiny-fx.scene-demo/slot-descriptors)

^#^{:doc "Starts the renderer thread. Usage: (start-renderer!) or (start-renderer! (slot-descriptors)).
Returns true on success, false when unsupported in the active runtime backend."}
(def start-renderer! tiny-clj.runtime/start-renderer!)

^#^{:doc "Stops the renderer thread. Usage: (stop-renderer!).
Returns true on success, false when unsupported in the active runtime backend."}
(def stop-renderer! tiny-clj.runtime/stop-renderer!)

^#^{:doc "Returns resolved render-thread transform state for one entity in one slot.
Returns nil when no captured render state exists for the slot/entity pair.
Usage: (renderer-state :game 3001) where `:game` is one configured slot id."}
(def renderer-state tiny-clj.runtime/renderer-state)

^#^{:doc "Returns active timeline keyframe index for one entity field.
Returns nil when the field has no captured timeline sample.
Usage: (renderer-timeline-step :game 3001 :t) where `:game` is one configured slot id."}
(def renderer-timeline-step tiny-clj.runtime/renderer-timeline-step)

^#^{:doc "Returns timeline phase metadata map for one entity field.
Returns nil when the field has no captured timeline sample.
Usage: (renderer-timeline-progress :game 3001 :t) where `:game` is one configured slot id."}
(def renderer-timeline-progress tiny-clj.runtime/renderer-timeline-progress)

^#^{:doc "Builds and returns host-viewer startup config map:
{:slots [{:id :deco :atom ...} ...]
 :spatial-callback tiny-fx.gfx/invoke-collision-callback!
 :game-scene-atom tiny-fx.scene-demo/game-scene-state}
Used by native host-viewer startup. The C host loop consumes only this config map,
so callback dispatch and live game-scene updates stay app-defined on the Clojure side."}
(def host-viewer-config
  (fn host-viewer-config []
    (tiny-fx.scene-demo/create-demo-bundle)
    {:slots (slot-descriptors)
     :spatial-callback tiny-fx.gfx/invoke-collision-callback!
     :game-scene-atom tiny-fx.scene-demo/game-scene-state}))

)TINY_GFX_RUNTIME"

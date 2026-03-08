R"TINY_GFX_RUNTIME(
(ns tiny-fx.gfx
  (:require [tiny-clj.runtime]
            [tiny-fx.gfx-scene]
            [tiny-fx.gfx-collision]
            [tiny-fx.game-demo]))

;; Direct var aliases to tiny-clj.runtime (no forwarding wrapper functions).
;; This keeps arity/error behavior identical to the native runtime entry points.

^#^{:doc "Benchmarks vector-scene decode+render path and returns a metrics map.
Usage: (vector-scene-bench) or (vector-scene-bench iterations warmup)."}
(def vector-scene-bench tiny-clj.runtime/vector-scene-bench)

;; Re-export the compact scene/collision API from the internal helper namespaces.
(def ->Transform tiny-fx.gfx-scene/->Transform)
(def ->Style tiny-fx.gfx-scene/->Style)
(def ->Group tiny-fx.gfx-scene/->Group)
(def ->Line tiny-fx.gfx-scene/->Line)
(def ->Polyline tiny-fx.gfx-scene/->Polyline)
(def ->Rect tiny-fx.gfx-scene/->Rect)
(def ->Tri tiny-fx.gfx-scene/->Tri)
(def ->VText tiny-fx.gfx-scene/->VText)
(def ->Timeline tiny-fx.gfx-scene/->Timeline)
(def ->Scene tiny-fx.gfx-scene/->Scene)
(def ->FrameScene tiny-fx.gfx-scene/->FrameScene)
(def ->CollisionRule tiny-fx.gfx-scene/->CollisionRule)
(def ->CollisionEvent tiny-fx.gfx-scene/->CollisionEvent)
(def ->SpatialRule tiny-fx.gfx-scene/->SpatialRule)
(def ->Aabb tiny-fx.gfx-scene/->Aabb)
(def ->SpatialEvent tiny-fx.gfx-scene/->SpatialEvent)
(def color tiny-fx.gfx-scene/color)
(def rgb888->color tiny-fx.gfx-scene/rgb888->color)
(def web-hex->color tiny-fx.gfx-scene/web-hex->color)
(def normalize-collision-phase-mask tiny-fx.gfx-scene/normalize-collision-phase-mask)
(def normalize-collision-rule tiny-fx.gfx-scene/normalize-collision-rule)
(def normalize-spatial-rule tiny-fx.gfx-scene/normalize-spatial-rule)
(def update-nodes tiny-fx.gfx-scene/update-nodes)
(def set-collision-callback! tiny-fx.gfx-collision/set-collision-callback!)
(def invoke-collision-callback! tiny-fx.gfx-collision/invoke-collision-callback!)
(def player-vs-obstacle-policy tiny-fx.gfx-collision/player-vs-obstacle-policy)

^#^{:doc "Returns the canonical ordered slot descriptor vector used by the runtime
and game-demo. Each slot descriptor contains at least `:id` and `:atom`.
Usage: (slot-descriptors)."}
(def slot-descriptors tiny-fx.game-demo/slot-descriptors)

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

^#^{:doc "Builds and returns game-demo startup config map:
{:slots [{:id :deco :atom ...} ...]
 :spatial-callback tiny-fx.gfx/invoke-collision-callback!
 :game-scene-atom tiny-fx.game-demo/game-scene-state}
Used by native game-demo startup. The C host loop consumes only this config map,
so callback dispatch and live game-scene updates stay app-defined on the Clojure side."}
(def game-demo-config
  (fn game-demo-config []
    (tiny-fx.game-demo/create-demo-bundle)
    {:slots (slot-descriptors)
     :spatial-callback tiny-fx.gfx/invoke-collision-callback!
     :game-scene-atom tiny-fx.game-demo/game-scene-state}))

)TINY_GFX_RUNTIME"

R"TINY_GFX_RUNTIME(
(ns tiny-gfx.runtime
  (:require [tiny-clj.runtime]
            [tiny-gfx.host-viewer-demo]
            [tiny-gfx.collision]))

;; Direct var aliases to tiny-clj.runtime (no forwarding wrapper functions).
;; This keeps arity/error behavior identical to the native runtime entry points.

^#^{:doc "Benchmarks vector-scene decode+render path and returns a metrics map.
Usage: (vector-scene-bench) or (vector-scene-bench iterations warmup)."}
(def vector-scene-bench tiny-clj.runtime/vector-scene-bench)

^#^{:doc "Starts the renderer thread. Usage: (start-renderer!) or (start-renderer! [game-slot score-slot deco-slot]).
Returns true on success, false when unsupported in the active runtime backend."}
(def start-renderer! tiny-clj.runtime/start-renderer!)

^#^{:doc "Stops the renderer thread. Usage: (stop-renderer!).
Returns true on success, false when unsupported in the active runtime backend."}
(def stop-renderer! tiny-clj.runtime/stop-renderer!)

^#^{:doc "Returns resolved render-thread transform state for one entity in one slot.
Returns nil when no captured render state exists for the slot/entity pair.
Usage: (renderer-state :game 3001)."}
(def renderer-state tiny-clj.runtime/renderer-state)

^#^{:doc "Returns active timeline keyframe index for one entity field.
Returns nil when the field has no captured timeline sample.
Usage: (renderer-timeline-step :game 3001 :t)."}
(def renderer-timeline-step tiny-clj.runtime/renderer-timeline-step)

^#^{:doc "Returns timeline phase metadata map for one entity field.
Returns nil when the field has no captured timeline sample.
Usage: (renderer-timeline-progress :game 3001 :t)."}
(def renderer-timeline-progress tiny-clj.runtime/renderer-timeline-progress)

^#^{:doc "Builds and returns host-viewer startup config map:
{:bundle [deco-scene score-scene game-scene]
 :collision-policy [...]
 :collision-entity-ids [...]}
Used by native host-viewer startup to avoid demo-specific C wiring."}
(def host-viewer-config
  (fn host-viewer-config []
    {:bundle (tiny-gfx.host-viewer-demo/create-demo-bundle)
     :collision-policy (tiny-gfx.collision/player-vs-obstacle-policy)
     :collision-entity-ids (tiny-gfx.host-viewer-demo/collision-entity-ids)}))

)TINY_GFX_RUNTIME"

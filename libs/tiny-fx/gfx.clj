
(ns tiny-fx.gfx
  (:require [tiny-clj.runtime]))

;; Direct var aliases to tiny-clj.runtime (no forwarding wrapper functions).
;; This keeps arity/error behavior identical to the native runtime entry points.

^#^{:doc "Starts the renderer thread. Usage: (start-renderer!) or (start-renderer! slot-descriptors).
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



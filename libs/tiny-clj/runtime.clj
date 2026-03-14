;; tiny-clj.runtime - Runtime debugging utilities
;; Provides low-level debugging functions for inspecting internal structures

(ns tiny-clj.runtime)

;; stats - Return runtime stats map
^#^{:doc "Returns runtime stats as a map.

Keys (always present):
- :host-os (string)
- :host-os-version (string)
- :tiny-clj-version (string)
- :build-time (#inst ...)

Optional keys (only present when the platform provides the value):
- :heap-bytes-free (integer, bytes)
- :heap-bytes-total (integer, bytes)
- :flash-bytes-free (integer, bytes; Flash-Tree partition, app-usable)
- :flash-bytes-total (integer, bytes; Flash-Tree partition total, app-usable)

Missing values are omitted (the key will not be present)."}
(def stats (fn stats [] :native))

;; Backward compatibility: alias
(def print-stats
  (fn print-stats []
    (stats)))

;; print-ast - Print AST structure with internals for debugging
;; Only available in DEBUG builds
^#^{:doc "Prints the AST (Abstract Syntax Tree) structure of an object with internal type information. Only available in DEBUG builds. Usage: (print-ast obj)"}
(def print-ast (fn print-ast [x] :native))

;; ast-string - Return AST structure as a string
;; Only available in DEBUG builds
^#^{:doc "Returns the AST (Abstract Syntax Tree) structure of an object as a string with internal type information. Only available in DEBUG builds. Usage: (ast-string obj)"}
(def ast-string (fn ast-string [x] :native))

^#^{:doc "Starts the renderer thread. Usage: (start-renderer!) or (start-renderer! slot-descriptors).
Returns true on success, false when unsupported in the active runtime backend."}
(def start-renderer! (fn start-renderer! [& args] :native))

^#^{:doc "Stops the renderer thread. Usage: (stop-renderer!).
Returns true on success, false when unsupported in the active runtime backend."}
(def stop-renderer! (fn stop-renderer! [] :native))

^#^{:doc "Returns resolved render-thread transform state for one entity in one slot.
Returns nil when no captured render state exists for the slot/entity pair.
Usage: (renderer-state :game 3001) where `:game` is one configured slot id."}
(def renderer-state (fn renderer-state [slot-id entity-id] :native))

^#^{:doc "Returns active timeline keyframe index for one entity field.
Returns nil when the field has no captured timeline sample.
Usage: (renderer-timeline-step :game 3001 :t) where `:game` is one configured slot id."}
(def renderer-timeline-step (fn renderer-timeline-step [slot-id entity-id field-key] :native))

^#^{:doc "Returns timeline phase metadata map for one entity field.
Returns nil when the field has no captured timeline sample.
Usage: (renderer-timeline-progress :game 3001 :t) where `:game` is one configured slot id."}
(def renderer-timeline-progress
  (fn renderer-timeline-progress [slot-id entity-id field-key] :native))

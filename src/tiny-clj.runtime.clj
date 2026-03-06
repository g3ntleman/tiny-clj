

R"TINY_CLJ_RUNTIME(
;; tiny-clj.runtime - Runtime debugging utilities
;; Provides low-level debugging functions for inspecting internal structures

(ns tiny-clj.runtime)

;; stats - Return runtime stats map
^#^{:doc "Returns runtime stats as a map.

Keys (always present):
- :os (string, e.g. \"darwin\", \"ESP/IDF\")
- :version (string, tiny-clj version)
- :build-time (#inst ...) when available

Optional:
- :os-version (string, e.g. macOS \"14.2.1\", ESP-IDF \"v5.3.4\")
- :gpio-event-drops (integer, count of dropped GPIO ISR events due to full ring buffer)
- :audio-cmd-drop-count (integer, dropped audio commands due to full command queue)
- :audio-tick-overrun-count (integer, ticks that hit bounded work limits)
- :audio-queue-high-watermark (integer, max pending audio command queue depth)
- :audio-sfx-drop-count (integer, dropped one-shot SFX triggers)
- :audio-finished-drop-count (integer, dropped finished notifications due to full queue)

Optional keys (only present when the platform provides the value):
- :heap-bytes-free (integer, bytes)
- :heap-bytes-total (integer, bytes)
- :external-ram-total (integer, bytes; optional external RAM e.g. PSRAM total)
- :flash-bytes-free (integer, bytes; Flash-Tree partition, app-usable)
- :flash-bytes-total (integer, bytes; Flash-Tree partition total, app-usable)

Optional flat keys under :hardware (no nesting):
- :model (string, e.g. \"ESP32\")
- :cores (integer)
- :revision (integer)
- :gpio-pin-count (integer)
- :psram-bytes (integer, total PSRAM when available)
- :wifi, :ble, :bt, :emb-flash, :emb-psram, :ieee802154 (boolean true/false when SoC reports features)

Missing values are omitted (the key will not be present)."}
(def stats (fn stats [] :native))

;; Backward compatibility: alias
(def print-stats
  (fn print-stats []
    (stats)))

;; vector-scene-bench - Benchmark decode+render path for demo scenes.
;; Returns a map with total-ms and us-per-frame metrics for deco/score/game scenes.
;; Optional args: (vector-scene-bench iterations warmup)
^#^{:doc "Benchmarks vector scene decode+render path and returns a metrics map. Usage: (vector-scene-bench) or (vector-scene-bench iterations warmup)."}
(def vector-scene-bench (fn vector-scene-bench [& args] :native))

;; renderer lifecycle API (M9/9j)
;; start-renderer! returns true on successful start (or already running),
;; false when no renderer backend is available in the current runtime.
^#^{:doc "Starts the renderer thread. Usage: (start-renderer!) or (start-renderer! [{:id :game :atom game-scene*} ...]). Returns true on success, false when unsupported."}
(def start-renderer! (fn start-renderer! [& args] :native))

;; stop-renderer! returns true on successful stop (or already stopped),
;; false when no renderer backend is available in the current runtime.
^#^{:doc "Stops the renderer thread. Usage: (stop-renderer!). Returns true on success, false when unsupported."}
(def stop-renderer! (fn stop-renderer! [] :native))

;; rendered-state query API (M9/9i baseline)
^#^{:doc "Returns current rendered transform/matrix state for one entity in one slot. Returns nil when slot/entity has no captured render state. Usage: (renderer-state :game 3001) where `:game` is a configured slot id."}
(def renderer-state (fn renderer-state [slot entity-id] :native))

^#^{:doc "Returns current active timeline keyframe index for one entity field. Returns nil when no timeline sample exists for this field (miss or non-Timeline field). Usage: (renderer-timeline-step :game 3001 :t) where `:game` is a configured slot id."}
(def renderer-timeline-step (fn renderer-timeline-step [slot entity-id field] :native))

^#^{:doc "Returns timeline phase metadata map for one entity field. Returns nil when no timeline sample exists for this field (miss or non-Timeline field). Usage: (renderer-timeline-progress :game 3001 :pts) where `:game` is a configured slot id."}
(def renderer-timeline-progress (fn renderer-timeline-progress [slot entity-id field] :native))

;; print-ast - Print AST structure with internals for debugging
;; Only available in DEBUG builds
^#^{:doc "Prints the AST (Abstract Syntax Tree) structure of an object with internal type information. Only available in DEBUG builds. Usage: (print-ast obj)"}
(def print-ast (fn print-ast [x] :native))

;; ast-string - Return AST structure as a string
;; Only available in DEBUG builds
^#^{:doc "Returns the AST (Abstract Syntax Tree) structure of an object as a string with internal type information. Only available in DEBUG builds. Usage: (ast-string obj)"}
(def ast-string (fn ast-string [x] :native))

)TINY_CLJ_RUNTIME"

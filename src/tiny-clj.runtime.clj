

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

;; print-ast - Print AST structure with internals for debugging
;; Only available in DEBUG builds
^#^{:doc "Prints the AST (Abstract Syntax Tree) structure of an object with internal type information. Only available in DEBUG builds. Usage: (print-ast obj)"}
(def print-ast (fn print-ast [x] :native))

;; ast-string - Return AST structure as a string
;; Only available in DEBUG builds
^#^{:doc "Returns the AST (Abstract Syntax Tree) structure of an object as a string with internal type information. Only available in DEBUG builds. Usage: (ast-string obj)"}
(def ast-string (fn ast-string [x] :native))

)TINY_CLJ_RUNTIME"

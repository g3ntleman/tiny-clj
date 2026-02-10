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



R"TINY_CLJ_RUNTIME(
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

^#^{:doc "Pretty-prints runtime stats with sensible margins and sorted keys (more readable nested :memory-stats)."}
(def pprint-stats
  (fn pprint-stats []
    (require 'clojure.pprint)
    (let [sort-map (fn [m] (when m (into (sorted-map) m)))
          s (stats)
          s (-> s
                (update :memory-stats
                        (fn [ms]
                          (some-> ms
                                  (update :bytes-by-type
                                          (fn [bt]
                                            (when bt
                                              (into (sorted-map)
                                                    (for [[k v] bt]
                                                      [k (sort-map v)])))))
                                  sort-map)))
                sort-map)]
      (binding [clojure.pprint/*print-right-margin* 90
                clojure.pprint/*print-miser-width* 60]
        (clojure.pprint/pprint s)))))

;; Backward compatibility: alias
(def print-stats
  (fn print-stats []
    (pprint-stats)))

;; print-ast - Print AST structure with internals for debugging
;; Only available in DEBUG builds
^#^{:doc "Prints the AST (Abstract Syntax Tree) structure of an object with internal type information. Only available in DEBUG builds. Usage: (print-ast obj)"}
(def print-ast (fn print-ast [x] :native))

;; ast-string - Return AST structure as a string
;; Only available in DEBUG builds
^#^{:doc "Returns the AST (Abstract Syntax Tree) structure of an object as a string with internal type information. Only available in DEBUG builds. Usage: (ast-string obj)"}
(def ast-string (fn ast-string [x] :native))

)TINY_CLJ_RUNTIME"

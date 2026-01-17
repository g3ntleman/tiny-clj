(ns tinyclj.fs)

;; Native-backed, flash-friendly filesystem API.
;; These are stubs whose implementation lives in C (see native_function_lookup).
;; Note: No explicit mkdir needed - directories are implicit in the KV store.

^#^{:doc "Writes bytes to path (native). Overwrites existing file."}
(defn spit-bytes [path bytes] :native)
^#^{:doc "Reads bytes from path (native). Returns a byte-array."}
(defn slurp-bytes [path] :native)
^#^{:doc "Returns file metadata for path (native)."}
(defn stat [path] :native)
^#^{:doc "Lists directory entries for dir-path (native)."}
(defn list [dir-path] :native)
^#^{:doc "Deletes a file or directory at path (native)."}
(defn delete! [path] :native)

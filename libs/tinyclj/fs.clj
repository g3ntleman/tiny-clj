(ns tinyclj.fs)

;; Native-backed, flash-friendly filesystem API.
;; These are stubs whose implementation lives in C (see native_function_lookup).

(defn mkdir [path] :native)
(defn spit-bytes [path bytes] :native)
(defn slurp-bytes [path] :native)
(defn stat [path] :native)
(defn list [dir-path] :native)
(defn delete! [path] :native)

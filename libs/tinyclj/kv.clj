(ns tinyclj.kv)

;; Native-backed KV API (separate key-space from tinyclj.fs).
;; Keys here must NOT start with "/".

(defn put-bytes [key bytes] :native)
(defn get-bytes [key] :native)
(defn delete! [key] :native)


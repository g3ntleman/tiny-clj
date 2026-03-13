
(ns tiny-db.kv)

;; Native-backed KV API (separate key-space from tiny-clj.fs).
;; Keys here must NOT start with "/".

^#^{:doc "Stores bytes under key in the tiny-db KV store (native). Keys must not start with \"/\"."}
(defn put-bytes [key bytes] :native)
^#^{:doc "Fetches bytes for key from the tiny-db KV store (native). Returns nil if missing."}
(defn get-bytes [key] :native)
^#^{:doc "Deletes key from the tiny-db KV store (native)."}
(defn delete! [key] :native)


;; tiny-clj.fs - Clojure-facing filesystem API
(ns tiny-clj.fs
  (:require [tiny-db.kv :as kv]))

;; Set user metadata for a path (merged into listing). Stored as EDN in an internal sidecar.
;; You may include date fields like :modified or :created; these are merged into the
;; reported :meta for the path and take precedence over optional/formal fields.
^#^{:doc "Stores user metadata for a filesystem path.

Writes the EDN-encoded metadata map to an internal sidecar in the underlying KV store.
The metadata is merged into `tiny-clj.fs/list` results.

If the map includes `:size`, the file size is applied immediately via `set-size!`."}
(defn- meta-put! [path meta-value] :native)

(defn- meta-get [path] :native)

(defn meta-set! [path meta-map]
  (let [_ (meta-put! path meta-map)]
    ;; If user provided a :size field, apply it immediately via native helper.
    (let [s (:size meta-map)]
      (if s
        (set-size! path s)
        nil))
    nil))

;; Native-backed, flash-friendly filesystem API. These are thin stubs whose
;; implementation is in native code (see native_function_lookup / src/fs_layer.c).
;; Directories are implicit in the KV store; no mkdir is required.

^#^{:doc "Writes a byte-array to `path` (native).

Overwrites any existing file. If `bytes` is nil, deletes the file.

Directories are implicit; no mkdir is required."}
(defn spit-bytes [path bytes] :native)

^#^{:doc "Reads the full contents of `path` (native).

Returns a byte-array, or nil if the path does not exist."}
(defn slurp-bytes [path] :native)

^#^{:doc "Returns file metadata for `path` (native).

Returns a map with at least:
- `:path`  (string)
- `:size`  (bytes)
- `:type`  (currently `:file`)"}
(defn stat [path] :native)

;; Internal native: list directory entries in batches.
;; Returns {:entries [ {:path <string> :meta <map>} ... ] :last-key <string-or-nil>}
(defn- list-batch [dir-path after-key batch-size] :native)

(defn- merge-entry-meta [entry]
  (let [formal-meta (or (:meta entry) {})
        edn-str (meta-get (:path entry))
        user-meta (if edn-str
                    (try
                      (read-string edn-str)
                      (catch Exception _ nil))
                    nil)]
    (assoc entry :meta (if (map? user-meta)
                         (merge formal-meta user-meta)
                         formal-meta))))

^#^{:doc "Lists direct children under `dir-path`.

Returns a lazy sequence of entry maps:
`{:path <string> :meta <map>}`.

The `:meta` map contains formal metadata (e.g. `:size`, `:chunks`) and may include
user-provided metadata stored via `meta-set!`."}
(defn list [dir-path]
  (let [batch-size 32]
    ((fn fetch [after-key]
       (lazy-seq
         (let [res (list-batch dir-path after-key batch-size)
             entries (seq (map merge-entry-meta (get res :entries)))
               last-key (get res :last-key)
               emit (fn emit [es]
                      (lazy-seq
                        (if (seq es)
                          (cons (first es) (emit (rest es)))
                          (if last-key
                            (fetch last-key)
                            nil))))]
           (emit entries))))
     nil)))

;; Block-level primitives
^#^{:doc "Reads up to `length` bytes from `path` starting at `offset` (native).

Returns a byte-array (possibly shorter than requested)."}
(defn read-block [path offset length] :native)

^#^{:doc "Writes `bytes` into `path` starting at `offset` (native).

Extends the file if needed. Returns an updated metadata map."}
(defn write-block [path offset bytes] :native)

^#^{:doc "Sets the file size for `path` to `size` (native).

If extended, missing blocks are reserved (zero-filled). If shrunk, excess blocks are freed.
Returns an updated metadata map."}
(defn set-size! [path size] :native)

;; Note: `delete!` removed; use `(spit-bytes path nil)` to delete a file.

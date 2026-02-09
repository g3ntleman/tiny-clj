R"TINY_CLJ_FS(
;; tiny-clj.fs - Clojure-facing filesystem API
(ns tiny-clj.fs
  (:require [tiny-db.kv :as kv]))

;; Set user metadata for a path (merged into listing). Stored as EDN under <path>.meta.
;; You may include date fields like :modified or :created; these are merged into the
;; reported :meta for the path and take precedence over optional/formal fields.
^#^{:doc "Stores user metadata for a filesystem path.

Writes the EDN-encoded metadata map to `<path>.meta` in the underlying KV store.
The metadata is merged into `tiny-clj.fs/list` results.

If the map includes `:size`, the file size is applied immediately via `set-size!`."}
(defn meta-set! [path meta-map]
  (let [edn-str (pr-str meta-map)
        bytes (.getBytes edn-str "UTF-8")]
    (kv/put-bytes (str path ".meta") bytes)
    ;; If user provided a :size field, apply it immediately via native helper.
    (when-let [s (:size meta-map)]
      (set-size! path s))
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
               entries (seq (get res :entries))
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

)TINY_CLJ_FS"

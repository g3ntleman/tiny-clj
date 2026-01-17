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
^#^{:doc "Internal: lists directory entries for dir-path in batches (native). Returns {:entries [...] :last-key k-or-nil}."}
(defn list-batch [dir-path after-key batch-size] :native)

^#^{:doc "Lists directory entries for dir-path. Returns a lazy sequence of paths."}
(defn list [dir-path]
  (let [batch-size 32]
    ((fn fetch [after-key]
       (lazy-seq
         (let [res (tinyclj.fs/list-batch dir-path after-key batch-size)
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
^#^{:doc "Deletes a file or directory at path (native)."}
(defn delete! [path] :native)

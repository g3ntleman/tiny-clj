;; Leak sanity test: measure :bytes-current before/after allocation-heavy work.
;; Run:  tiny-clj-repl -f scripts/leak_test.clj
;; Or:   tiny-clj-repl -e "(require 'tinyclj.runtime)" \
;;         -e "(def b1 (get (get (tinyclj.runtime/stats) :memory-stats) :bytes-current))" \
;;         -e "(println \"before:\" b1)" \
;;         -e "(dotimes [_ 20] (reduce + (range 4000)))" \
;;         -e "(def b2 (get (get (tinyclj.runtime/stats) :memory-stats) :bytes-current))" \
;;         -e "(println \"after:\" b2)" \
;;         -e "(when (and b1 b2) (println \"delta:\" (- b2 b1)))"
;; Requires DEBUG and MEMORY_PROFILING_ENABLED for :memory-stats.

(require 'tinyclj.runtime)

(defn ms [k]
  (get (get (tinyclj.runtime/stats) :memory-stats) k))

(def b1 (ms :bytes-current))
(println "bytes-current before:" (or b1 "n/a (profiling off?)"))

(dotimes [_ 20] (reduce + (range 4000)))

(def b2 (ms :bytes-current))
(println "bytes-current after 20x (reduce + (range 4000)):" (or b2 "n/a"))

(dotimes [_ 10] (tinyclj.runtime/stats))

(def b3 (ms :bytes-current))
(println "bytes-current after 10x (tinyclj.runtime/stats):" (or b3 "n/a"))

(if (and b1 b2 b3)
  (do
    (println "delta b2-b1:" (- b2 b1))
    (println "delta b3-b1:" (- b3 b1))
    (println "ok: deltas near 0 => no leak; large growth => possible leak"))
  (println "skip: need DEBUG and MEMORY_PROFILING_ENABLED for :memory-stats"))

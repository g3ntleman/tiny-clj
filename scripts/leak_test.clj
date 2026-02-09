;; Leak sanity test: measure :bytes-current before/after allocation-heavy work.
;; Uses separate top-level forms so each runs in its own autorelease pool;
;; delta then reflects leak per eval (linear growth => possible leak).
;; Run:  tiny-clj-repl -f scripts/leak_test.clj
;; Requires DEBUG and MEMORY_PROFILING_ENABLED for :memory-stats.

(require 'tiny-clj.runtime)

(defn ms [k]
  (get (get (tiny-clj.runtime/stats) :memory-stats) k))

(def b0 (ms :bytes-current))
(println "bytes-current before (baseline):" (or b0 "n/a (profiling off?)"))

;; 20 separate evals of (reduce + (range 500)) – each form = own pool
(reduce + (range 500))
(reduce + (range 500))
(reduce + (range 500))
(reduce + (range 500))
(reduce + (range 500))
(reduce + (range 500))
(reduce + (range 500))
(reduce + (range 500))
(reduce + (range 500))
(reduce + (range 500))
(reduce + (range 500))
(reduce + (range 500))
(reduce + (range 500))
(reduce + (range 500))
(reduce + (range 500))
(reduce + (range 500))
(reduce + (range 500))
(reduce + (range 500))
(reduce + (range 500))
(reduce + (range 500))

(def b1 (ms :bytes-current))
(println "bytes-current after 20 separate (reduce + (range 500)):" (or b1 "n/a"))

;; 10 separate evals of (tiny-clj.runtime/stats)
(tiny-clj.runtime/stats)
(tiny-clj.runtime/stats)
(tiny-clj.runtime/stats)
(tiny-clj.runtime/stats)
(tiny-clj.runtime/stats)
(tiny-clj.runtime/stats)
(tiny-clj.runtime/stats)
(tiny-clj.runtime/stats)
(tiny-clj.runtime/stats)
(tiny-clj.runtime/stats)

(def b2 (ms :bytes-current))
(println "bytes-current after 10 separate (tiny-clj.runtime/stats):" (or b2 "n/a"))

(if (and b0 b1 b2)
  (do
    (println "delta reduce (b1-b0):" (- b1 b0))
    (println "delta stats (b2-b1):" (- b2 b1))
    (println "delta total (b2-b0):" (- b2 b0))
    (println "ok: deltas near 0 => no leak; linear growth => possible leak per eval"))
  (println "skip: need DEBUG and MEMORY_PROFILING_ENABLED for :memory-stats"))

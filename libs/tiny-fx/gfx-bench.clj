R"TINY_GFX_BENCH(
(ns tiny-fx.gfx-bench)

^#^{:doc "DEBUG/profiling-only benchmark for vector-scene decode+render.
Returns a metrics map. Usage: (vector-scene-bench) or (vector-scene-bench iterations warmup)."}
(def vector-scene-bench (fn vector-scene-bench [& args] :native))

)TINY_GFX_BENCH"

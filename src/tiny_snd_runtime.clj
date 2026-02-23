R"TINY_SND_RUNTIME(
(ns tiny-snd.runtime)
;; Runtime API is registered from C (builtins) into this namespace.
;; Use (require '[tiny-snd.runtime :refer :all]) or tiny-snd.runtime/audio-load-track! etc.
)TINY_SND_RUNTIME"

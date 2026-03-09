R"TINY_SND_DEBUG(
(ns tiny-fx.sound-debug)

^#^{:doc "DEBUG-only sound host diagnostics map. Not part of the production sound API."}
(def host-status! (fn host-status! [] :native))

^#^{:doc "DEBUG-only one-shot test tone helper. Not part of the production sound API."}
(def play-test-tone! (fn play-test-tone! [& args] :native))

)TINY_SND_DEBUG"

R"TINY_SND_DEBUG(
(ns tiny-fx.sound-debug)

^#^{:doc "DEBUG-only sound host diagnostics map. Not part of the production sound API."}
(def host-status! (fn host-status! [] :native))

^#^{:doc "DEBUG-only one-shot test tone helper. Not part of the production sound API."}
(def play-test-tone! (fn play-test-tone! [& args] :native))

^#^{:doc "DEBUG-only host pseudo-noise helper. Not part of the production sound API."}
(def play-test-noise! (fn play-test-noise! [& args] :native))

^#^{:doc "DEBUG-only host linear frequency ramp helper. Example: (play-test-ramp! 220 320 3000 220)."}
(def play-test-ramp! (fn play-test-ramp! [& args] :native))

^#^{:doc "DEBUG-only host ramp-noise helper for thruster-like sweeps. Example: (play-test-ramp-noise! 220 300 3000 3 220)."}
(def play-test-ramp-noise! (fn play-test-ramp-noise! [& args] :native))

^#^{:doc "DEBUG-only musical portamento reference built on the host ramp helper."}
(defn play-portamento-reference! []
  (play-test-ramp! 220 330 1800 200))

^#^{:doc "DEBUG-only rocket/thruster reference built as short host ramps instead of discrete notes."}
(defn play-rocket-thruster-reference! []
  (play-test-ramp-noise! 180 360 2200 35 220))

^#^{:doc "DEBUG-only thrust demo: best compromise so far. Two-segment pseudo-noise, lower bands, 4 ms hops. Not for production."}
(defn play-thrust-demo! []
  (play-test-noise! 90 285 1400 4 228)
  (Thread/sleep 1370)
  (play-test-noise! 50 345 1400 4 232)
  (Thread/sleep 1700)
  nil)

)TINY_SND_DEBUG"

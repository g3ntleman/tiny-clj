
(ns tiny-fx.sound-debug
  (:require [tiny-fx.sound :as sound]))

^#^{:doc "DEBUG-only sound host diagnostics map. Not part of the production sound API."}
(defn host-status! [] :native)

^#^{:doc "DEBUG-only one-shot test tone helper. Not part of the production sound API."}
(defn play-test-tone! [& args] :native)

^#^{:doc "DEBUG-only host pseudo-noise helper. Not part of the production sound API."}
(defn play-test-noise! [& args] :native)

^#^{:doc "DEBUG-only host linear frequency ramp helper. Example: (play-test-ramp! 220 320 3000 220)."}
(defn play-test-ramp! [& args] :native)

^#^{:doc "DEBUG-only host ramp-noise helper for thruster-like sweeps. Example: (play-test-ramp-noise! 220 300 3000 3 220)."}
(defn play-test-ramp-noise! [& args] :native)

^#^{:doc "DEBUG-only musical portamento reference built on the host ramp helper."}
(defn play-portamento-reference! []
  (play-test-ramp! 220 330 1800 200))

^#^{:doc "DEBUG-only rocket/thruster reference built as short host ramps instead of discrete notes."}
(defn play-rocket-thruster-reference! []
  (play-test-ramp-noise! 180 360 2200 35 220))

(def thrust-demo-track-id :debug-thrust-demo)
(def thrust-demo-steps
  [{:notes [90] :bend [285] :noise true :duration 1400}
   {:notes [50] :bend [345] :noise true :duration 1400}])
(def thrust-demo-opts
  {:channel-count 1
   :volumes [228]
   :gate-percent 100})

^#^{:doc "DEBUG-only thrust demo using the regular DSL/SFX path instead of host-only play-test-noise!."}
(defn play-thrust-demo! []
  (require 'tiny-fx.trk1)
  (let [prepared ((var tiny-fx.trk1/prepare-track) thrust-demo-steps thrust-demo-opts)]
    {:status (if (sound/sound-play-sfx! thrust-demo-track-id (:track-bytes prepared)) :playing :dropped)
     :duration-ms (:duration-ms prepared)}))

^#^{:doc "DEBUG-only piu demo: short up-ramp then down-ramp ending low (host bending). Not for production."}
(defn play-piu-demo! []
  (play-test-ramp! 1400 4000 18 218)
  (Thread/sleep 22)
  (play-test-ramp! 4000 750 48 218)
  (Thread/sleep 80)
  nil)

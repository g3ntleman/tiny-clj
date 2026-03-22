(ns tiny-clj.deployment
  (:require [tiny-breakout.runtime :as breakout-runtime]))

(defn breakout-host-config
  "Builds host-viewer config for the breakout demo with semantic button input.
  The startup callback skips the title screen and launches directly into :play."
  []
  (breakout-runtime/bootstrap-runtime!)
  {:slots [{:id :game :atom breakout-runtime/scene*}]
   :startup-callback breakout-runtime/start-runtime!
   :auto-launch-callback (fn [& _args]
                           (breakout-runtime/apply-input! {:launch true})
                           nil)
   :spatial-callback breakout-runtime/on-spatial-event!
   :game-scene-atom breakout-runtime/scene*
   :entry :tiny-breakout})

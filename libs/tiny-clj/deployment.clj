(ns tiny-clj.deployment
  (:require [tiny-breakout.runtime :as breakout-runtime]))

(defn breakout-host-config
  "Builds host-viewer config for the breakout demo with semantic button input."
  []
  (let [scene-atom breakout-runtime/scene*]
    {:slots [{:id :game :atom scene-atom}]
     :prepare-callback breakout-runtime/bootstrap-runtime!
     :startup-callback breakout-runtime/start-runtime!
     :auto-launch-callback (fn [& _args]
                             (breakout-runtime/apply-input! {:launch true})
                             nil)
     :spatial-callback breakout-runtime/on-spatial-event!
     :game-scene-atom scene-atom
     :entry :tiny-breakout}))

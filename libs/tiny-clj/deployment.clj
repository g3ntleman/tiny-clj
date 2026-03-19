(ns tiny-clj.deployment
  (:require [tiny-breakout.runtime :as breakout-runtime]))

(defn breakout-host-config
  "Builds host-viewer config for the breakout demo with semantic button input."
  []
  (breakout-runtime/reset-runtime!)
  {:slots [{:id :game :atom breakout-runtime/scene*}]
   :spatial-callback breakout-runtime/on-spatial-event!
   :game-scene-atom breakout-runtime/scene*
   :entry :tiny-breakout})

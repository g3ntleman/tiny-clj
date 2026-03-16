(ns tiny-fx.startup
  (:require [tiny-fx.gfx :as gfx]
            [tiny-fx.gfx-scene :refer [edn->scene]]
            [tiny-fx.assets :as assets]))

(def deco-scene-state (atom nil))
(def overlay-scene-state (atom nil))

(defn slot-descriptors
  []
  [{:id :deco :atom deco-scene-state}
   {:id :score :atom overlay-scene-state}])

(defn create-startup-bundle
  "Creates the global FX startup bundle and publishes fresh scene atoms."
  []
  (let [data (assets/load-edn-asset "/libs/tiny-fx/assets/startup.edn" [:deco-scene-template :overlay-scene-template])
        deco-scene-template (edn->scene (:deco-scene-template data))
        overlay-scene-template (edn->scene (:overlay-scene-template data))
        bundle [deco-scene-template overlay-scene-template]]
    (reset! deco-scene-state deco-scene-template)
    (reset! overlay-scene-state overlay-scene-template)
    bundle))

(defn start!
  "Creates the startup scenes and starts the runtime renderer with them."
  []
  (let [bundle (create-startup-bundle)
        slots (slot-descriptors)
        started? (gfx/start-renderer! slots)]
    {:bundle bundle
     :slots slots
     :started? started?}))

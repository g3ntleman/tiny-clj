(ns tiny-fx.sound-demos
  (:require [tiny-fx.assets :as assets]))

(def ^:private sfx-keys
  #{:rocket-launch-sfx :laser-sfx})

(defn load-song
  [which]
  (assoc (assets/edn-asset-under-prefix "tiny-fx/sound-demos"
                                                       (str (name which) ".edn")
                                                       [:track-id :steps :opts])
         :kind
         (if (contains? sfx-keys which) :sfx :music)))

(defn play-demo!
  [which]
  (require 'tiny-fx.sound)
  (let [d (load-song which)]
    ((var tiny-fx.sound/play!) d)))

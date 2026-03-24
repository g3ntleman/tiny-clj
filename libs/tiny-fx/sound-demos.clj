(ns tiny-fx.sound-demos
  (:require [tiny-fx.assets :as assets]))

(def ^:private sfx-keys
  #{:rocket-launch-sfx :laser-sfx})

(defn load-song
  "Loads one demo song descriptor with :track-id, :steps, :opts and inferred :kind."
  [which]
  (assoc (assets/edn-asset-under-prefix "tiny-fx/sound-demos"
                                                       (str (name which) ".edn")
                                                       [:track-id :steps :opts])
         :kind
         (if (contains? sfx-keys which) :sfx :music)))

(defn play-demo!
  "Compiles and plays one bundled demo via explicit trk1/runtime composition."
  [which]
  (require 'tiny-fx.sound)
  (require 'tiny-fx.trk1)
  (let [d (load-song which)
        prepared ((var tiny-fx.trk1/prepare-track) (:steps d) (:opts d))
        track-bytes (:track-bytes prepared)
        duration-ms (:duration-ms prepared)
        status (if (= (:kind d) :sfx)
                 (if ((var tiny-fx.sound/sound-play-sfx!) (:track-id d) track-bytes) :playing :dropped)
                 (if ((var tiny-fx.sound/sound-play-music!) (:track-id d) track-bytes 1) :playing :stopped))]
    {:status status
     :duration-ms duration-ms}))

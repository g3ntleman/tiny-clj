(ns tiny-fx.sound-demos
  (:require [tiny-fx.sound :as sound]))

(defn ensure-sound-demo-data-loaded!
  []
  (load-file "/libs/tiny-fx/sound-demos-data.clj"))

(defn demo
  [which]
  (ensure-sound-demo-data-loaded!)
  (tiny-fx.sound-demos-data/build-demo which))

(defn play-demo!
  [which]
  (let [d (demo which)]
    (when (nil? d)
      (throw "Unknown sound demo"))
    (if (tiny-fx.sound-demos-data/sfx-demo-key? which)
      (sound/play-sfx! (:track-id d) (:steps d) (:opts d))
      (sound/play-steps! (:track-id d) (:steps d) (:opts d)))))

(ns tiny-fx.sound-demos
  (:require [tiny-fx.assets :as assets]))

(defn demo
  [which]
  (let [path (str "/libs/tiny-fx/assets/sound-demos/" (name which) ".edn")]
    (assets/load-edn-asset path [:track-id :steps :opts])))

(defn sfx-demo-key?
  [which]
  (or (= which :rocket-launch-sfx)
      (= which :laser-sfx)))

(defn play-demo!
  [which]
  (require 'tiny-fx.sound)
  (let [d (demo which)]
    (when (nil? d)
      (throw "Unknown sound demo"))
    (if (sfx-demo-key? which)
      (tiny-fx.sound/play-sfx! (:track-id d) (:steps d) (:opts d))
      (tiny-fx.sound/play-steps! (:track-id d) (:steps d) (:opts d)))))

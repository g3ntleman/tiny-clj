
(ns tiny-fx.game-demo
  (:require [tiny-fx.gfx :as gfx]
            [tiny-fx.gfx-scene :refer [edn->scene]]
            [tiny-fx.gfx-collision :as collision]
            [tiny-fx.assets :as assets]
            [tiny-fx.sound-native :as sound]
            [tiny-clj.gpio :as gpio]))

(def player-entity-id 3002)
(def player-collision-entity-id 3006)
(def obstacle-entity-id 3003)
(def starwars-title-track-id :starwars-title-2v)
(def demo-melody-track-id :game-demo-melody)
(def demo-data* (atom nil))

(def deco-scene-state (atom nil))
(def score-scene-state (atom nil))
(def game-scene-state (atom nil))
(def demo-melody-trigger-count* (atom 0))
(def demo-launch-pin 1)
(def demo-launch-pressed* (atom false))
(def demo-input-watcher-active* (atom false))
(def demo-spatial-watcher-active* (atom false))

(defn slot-descriptors
  "Returns the canonical ordered slot descriptor vector for tiny-gfx runtime
and game-demo startup."
  []
  [{:id :deco :atom deco-scene-state}
   {:id :score :atom score-scene-state}
   {:id :game :atom game-scene-state}])

(defn load-demo-data! []
  (let [d @demo-data*]
    (if d
      d
      (let [raw (assets/edn-asset-under-prefix "tiny-fx" "game-demo.edn" nil)
            parsed (edn->scene raw)]
        (reset! demo-data* parsed)
        parsed))))

(defn play-starwars-title!
  "Plays the piezo-friendly Star Wars title phrase once."
  []
  (let [data (load-demo-data!)
        bytes (byte-array (:starwars-title-bytes data))]
    (sound/sound-load-track! starwars-title-track-id bytes)
    {:status (if (sound/sound-play-music! starwars-title-track-id 1) :playing :stopped)
     :duration-ms 8925}))

(defn player-jump-timeline-for-state
  [player-small?]
  (let [data (load-demo-data!)]
    (if player-small?
      (:player-small-jump-timeline data)
      (:player-jump-timeline data))))

(defn apply-player-scale
  [game-scene player-small?]
  (let [index (:index game-scene)
        player (get index player-entity-id)
        proxy (get index player-collision-entity-id)
        next-timeline (player-jump-timeline-for-state player-small?)]
    (if (or (nil? player)
            (nil? proxy)
            (and (= (:t player) next-timeline)
                 (= (:t proxy) next-timeline)))
      game-scene
      (let [updated-player (assoc player :t next-timeline)
            updated-proxy (assoc proxy :t next-timeline)
            next-index (assoc (assoc index player-entity-id updated-player) player-collision-entity-id updated-proxy)]
        ;; Keep scene as FrameScene record so native viewer accepts atom updates.
        (record-from-map 'FrameScene (assoc game-scene :index next-index))))))

(defn- ensure-frame-scene-root-index
  [scene]
  (let [root (:root scene)]
    (if (and (nil? (:index scene))
             (map? root)
             (contains? root 'root))
      (record-from-map 'FrameScene
                       (assoc scene
                              :root (get root 'root)
                              :index (dissoc root 'root)))
      scene)))

(defn- event->player-small-state
  [event]
  (if (= (:kind event) :collision)
    (cond
      (= (:phase event) :enter) true
      (= (:phase event) :exit) false
      :else :ignore)
    :ignore))

(defn on-player-collision-toggle!
  "Game-demo spatial callback: reacts to `:collision` `:enter`/`:exit`
by changing player scale.
Returns nil; host-side callback dispatch ignores return values."
  [event]
  (let [next-state (event->player-small-state event)]
    (when (not= next-state :ignore)
      (let [game-scene @game-scene-state]
        (when game-scene
          (reset! game-scene-state
                  (apply-player-scale game-scene next-state)))))
    nil))

(defn on-demo-input-event!
  "Game-demo input callback: a :button/down on :demo/launch triggers
a short melody from Clojure. Returns nil; host-side callback dispatch ignores
return values."
  [event]
  (let [pressed? (= 1 (gpio/read demo-launch-pin))]
    (cond
      (and pressed? (not @demo-launch-pressed*))
      (do
        (reset! demo-launch-pressed* true)
        (swap! demo-melody-trigger-count* inc)
        (let [data (load-demo-data!)
              bytes (byte-array (:demo-melody-bytes data))]
          (sound/sound-load-track! demo-melody-track-id bytes)
          (sound/sound-play-music! demo-melody-track-id 1)))
      (not pressed?)
      (reset! demo-launch-pressed* false)
      :else nil))
  nil)

(defn configure-demo-input-watchers!
  "Registers the demo event subscription used by the game demo input simulation."
  []
  (when @demo-input-watcher-active*
    (gpio/watch demo-launch-pin nil))
  (gpio/watch demo-launch-pin
            on-demo-input-event!)
  (reset! demo-input-watcher-active* true)
  nil)

(defn configure-demo-spatial-watchers!
  "Registers the demo spatial subscription through the generic event API."
  []
  (when @demo-spatial-watcher-active*
    (collision/watch :player-vs-rocket nil))
  (collision/watch :player-vs-rocket
            on-player-collision-toggle!)
  (reset! demo-spatial-watcher-active* true)
  nil)

(defn collision-entity-ids
  "Returns [player-collision-entity-id obstacle-entity-id] for game-demo collision state queries."
  []
  [player-collision-entity-id obstacle-entity-id])

(defn create-demo-bundle
  "Returns a vector with the demo frame-scenes used by the game demo.
Index layout:
0 deco-scene
1 score-scene
2 game-scene"
  []
  (let [data (load-demo-data!)
        deco-scene (ensure-frame-scene-root-index (:deco-scene-template data))
        score-scene (ensure-frame-scene-root-index (:score-scene-template data))
        game-entities (assoc (:game-entities-static data)
                        obstacle-entity-id (:rocket-body-instance data)
                        3005 (:rocket-nose-instance data)
                        player-collision-entity-id (:game-player-collision data)
                        player-entity-id (:game-player data))
        game-scene (record-create 'FrameScene [(get game-entities 'root)
                                                   (dissoc game-entities 'root)
                                                   [0 40 320 136]
                                                   2
                                                   true
                                                   true
                                                   0
                                                   1
                                                   [(:collision-rule data)]])
        demo-bundle [deco-scene score-scene game-scene]]
    (reset! deco-scene-state deco-scene)
    (reset! score-scene-state score-scene)
    (reset! game-scene-state game-scene)
    (reset! demo-melody-trigger-count* 0)
    (reset! demo-launch-pressed* false)
    (configure-demo-input-watchers!)
    (configure-demo-spatial-watchers!)
    demo-bundle))

(defn game-demo-config
  "Builds and returns native startup config for the game demo."
  []
  (create-demo-bundle)
  {:slots (slot-descriptors)
   :spatial-callback collision/invoke-collision-callback!
   :game-scene-atom game-scene-state})

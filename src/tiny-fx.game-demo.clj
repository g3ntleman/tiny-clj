R"TINY_GFX_HOST(
(ns tiny-fx.game-demo
  (:require [tiny-fx.gfx-scene :refer [->Transform ->Style ->Group ->Polyline
                                     ->Tri ->VText ->FrameScene ->Timeline ->SpatialRule color]]
            [tiny-fx.gfx-collision :as collision]
            [tiny-fx.audio :as sound]))

(defn style
  [{:keys [stroke-color stroke-width visible has-fill fill-color has-bg-color bg-color]
    :or   {stroke-color 0
           stroke-width 1
           visible true
           has-fill false
           fill-color 0
           has-bg-color false
           bg-color 0}}]
  (->Style stroke-color stroke-width visible has-fill fill-color has-bg-color bg-color))

(defn transform
  [{:keys [tx ty sx sy rot]
    :or   {tx 0 ty 0 sx 1 sy 1 rot 0}}]
  (->Transform tx ty sx sy rot))

(defn timeline
  [{:keys [keyframes loop]
    :or   {keyframes [] loop true}}]
  (->Timeline keyframes loop))

(defn polyline
  [{:keys [id t style visible pts closed]
    :or   {t nil visible true closed false}}]
  (->Polyline id t style visible pts closed))

(defn tri
  [{:keys [id t style visible x1 y1 x2 y2 x3 y3]
    :or   {t nil visible true}}]
  (->Tri id t style visible x1 y1 x2 y2 x3 y3))

(defn vtext
  [{:keys [id t style visible x y scale rot text]
    :or   {t nil visible true x 0 y 0 scale 1 rot 0 text ""}}]
  (->VText id t style visible x y scale rot text))

(defn group
  [{:keys [id t style visible children]
    :or   {t nil visible true children []}}]
  (->Group id t style visible children))

(defn frame-scene
  [{:keys [root clip-rect z visible opaque erase-color guard-px collision-rules]
    :or   {clip-rect [0 0 320 240]
           z 0
           visible true
           opaque true
           erase-color 0
           guard-px 1
           collision-rules nil}}]
  (->FrameScene root clip-rect z visible opaque erase-color guard-px collision-rules))

(def color-cyan (color 0x00FFFF))
(def color-white (color 0xFFFFFF))
(def color-black (color 0x000000))
(def color-green (color 0x00FF00))
(def color-magenta (color 0xFF00FF))
(def color-yellow (color 0xFFFF00))
(def color-red (color 0xFF0000))
(def root-id 'root)

(def style-deco (style {:stroke-color color-cyan :stroke-width 2}))
(def style-score (style {:stroke-color color-white :stroke-width 1}))
(def style-game-line (style {:stroke-color color-green :stroke-width 2}))
(def style-game-player (style {:stroke-color color-magenta :stroke-width 3}))
;; Hidden collision geometry stays in the rendered-state snapshot for host collision queries.
(def style-player-collision-proxy (style {:visible false}))
(def style-rocket-body (style {:stroke-color color-yellow :stroke-width 2 :has-fill true :fill-color color-yellow}))
(def style-rocket-nose (style {:stroke-color color-red :stroke-width 2 :has-fill true :fill-color color-red}))
(def style-hbar (style {:stroke-color color-white :stroke-width 1 :has-fill true :fill-color color-white}))
(def style-star-white (style {:stroke-color color-white :stroke-width 1 :has-fill true :fill-color color-white}))
(def style-star-black (style {:stroke-color color-black :stroke-width 1 :has-fill true :fill-color color-black}))
(def style-star-yellow (style {:stroke-color color-yellow :stroke-width 1 :has-fill true :fill-color color-yellow}))
(def style-star-cyan (style {:stroke-color color-cyan :stroke-width 1 :has-fill true :fill-color color-cyan}))

(def mountain-pts
  [[0 228] [26 206] [58 226] [92 196] [126 225]
   [162 202] [198 230] [238 192] [276 226] [319 204]])

(def rocket-body-pts
  [[-12 -4] [8 -4] [8 -10] [-12 -10]
   [-16 -14] [-20 -14] [-20 -9] [-13 -9]
   [-13 -8] [-20 -8] [-20 -6] [-13 -6]
   [-13 -5] [-20 -5] [-20 0] [-16 0]])

(def rocket-nose-geometry {:x1 20 :y1 -7 :x2 10 :y2 -10 :x3 10 :y3 -4})

(def player-geometry {:x1 -16 :y1 0 :x2 0 :y2 -28 :x3 16 :y3 0})
(def player-world-x 72)
(def player-world-y 146)
(def player-jump-height 10)
(def player-small-scale-x 0.75)
(def player-small-scale-y 0.7142857)

(def terrain-timeline
  (timeline {:keyframes [[0 (transform {:tx 0 :ty 0 :rot 0})]
                         [2666 (transform {:tx -320 :ty 0 :rot 0})]]
             :loop true}))

(def player-jump-timeline
  (timeline {:keyframes [[0 (transform {:tx player-world-x
                                        :ty player-world-y
                                        :sx 1
                                        :sy 1
                                        :rot 0})]
                         [133 (transform {:tx player-world-x
                                          :ty (- player-world-y player-jump-height)
                                          :sx 1
                                          :sy 1
                                          :rot 0})]
                         [266 (transform {:tx player-world-x
                                          :ty player-world-y
                                          :sx 1
                                          :sy 1
                                          :rot 0})]
                         [800 (transform {:tx player-world-x
                                          :ty player-world-y
                                          :sx 1
                                          :sy 1
                                          :rot 0})]]
             :loop true}))

(def player-small-jump-timeline
  (timeline {:keyframes [[0 (transform {:tx player-world-x
                                        :ty player-world-y
                                        :sx player-small-scale-x
                                        :sy player-small-scale-y
                                        :rot 0})]
                         [133 (transform {:tx player-world-x
                                          :ty (- player-world-y player-jump-height)
                                          :sx player-small-scale-x
                                          :sy player-small-scale-y
                                          :rot 0})]
                         [266 (transform {:tx player-world-x
                                          :ty player-world-y
                                          :sx player-small-scale-x
                                          :sy player-small-scale-y
                                          :rot 0})]
                         [800 (transform {:tx player-world-x
                                          :ty player-world-y
                                          :sx player-small-scale-x
                                          :sy player-small-scale-y
                                          :rot 0})]]
             :loop true}))

(defn- player-jump-timeline-for-state
  [player-small?]
  (if player-small?
    player-small-jump-timeline
    player-jump-timeline))

(def rocket-timeline
  (timeline {:keyframes [[0 (transform {:tx 346 :ty 126 :rot -90})]
                         [3000 (transform {:tx -14 :ty 126 :rot -90})]]
             :loop true}))

(def star-drift-timeline
  (timeline {:keyframes [[0 (transform {:tx 0 :ty 0 :rot 0})]
                         [16000 (transform {:tx -320 :ty 0 :rot 0})]]
             :loop true}))

(def score-text-timeline
  (timeline {:keyframes [[0 "SCORE 0000    LIFES 3"]
                         [1000 "SCORE 0120    LIFES 3"]
                         [2000 "SCORE 0240    LIFES 3"]
                         [3000 "SCORE 0360    LIFES 3"]
                         [4000 "SCORE 0480    LIFES 3"]
                         [5000 "SCORE 0600    LIFES 3"]
                         [6000 "SCORE 0720    LIFES 3"]
                         [7000 "SCORE 0840    LIFES 3"]
                         [8000 "SCORE 0960    LIFES 3"]
                         [9000 "SCORE 1080    LIFES 3"]]
             :loop true}))

(def star-style-1
  (timeline {:keyframes [[0 style-star-white] [900 style-star-black]
                         [2200 style-star-yellow] [3300 style-star-black]
                         [5200 style-star-cyan] [6600 style-star-black]
                         [8400 style-star-yellow] [10400 style-star-black]
                         [12400 style-star-white]]
             :loop true}))

(def star-style-2
  (timeline {:keyframes [[0 style-star-cyan] [760 style-star-black]
                         [1960 style-star-white] [3000 style-star-black]
                         [4600 style-star-yellow] [6000 style-star-black]
                         [7800 style-star-cyan] [9600 style-star-black]
                         [11800 style-star-white]]
             :loop true}))

(def star-style-3
  (timeline {:keyframes [[0 style-star-yellow] [1040 style-star-black]
                         [2400 style-star-white] [3500 style-star-black]
                         [5100 style-star-cyan] [6800 style-star-black]
                         [8600 style-star-white] [10300 style-star-black]
                         [12200 style-star-yellow] [14000 style-star-white]]
             :loop true}))

(def star-style-4
  (timeline {:keyframes [[0 style-star-white] [1200 style-star-black]
                         [2900 style-star-cyan] [4200 style-star-black]
                         [6300 style-star-yellow] [7800 style-star-black]
                         [10100 style-star-cyan] [11600 style-star-black]
                         [14000 style-star-white]]
             :loop true}))

(def star-style-5
  (timeline {:keyframes [[0 style-star-yellow] [940 style-star-black]
                         [2360 style-star-cyan] [3760 style-star-black]
                         [5600 style-star-white] [7100 style-star-black]
                         [9000 style-star-yellow] [10800 style-star-black]
                         [13000 style-star-cyan] [14800 style-star-white]]
             :loop true}))

(def star-style-6
  (timeline {:keyframes [[0 style-star-cyan] [1060 style-star-black]
                         [2700 style-star-yellow] [3960 style-star-black]
                         [6100 style-star-white] [7640 style-star-black]
                         [9920 style-star-cyan] [11480 style-star-black]
                         [13760 style-star-yellow] [15600 style-star-white]]
             :loop true}))

(def game-stars-group
  (group {:id 3020 :t star-drift-timeline :style style-star-white
          :children [3021 3022 3023 3024 3025 3026]}))

(def star-1 (polyline {:id 3021 :style star-style-1 :pts [[39 99] [39 101] [39 100] [38 100] [40 100]]}))
(def star-2 (polyline {:id 3022 :style star-style-2 :pts [[85 85] [85 87] [85 86] [84 86] [86 86]]}))
(def star-3 (polyline {:id 3023 :style star-style-3 :pts [[135 109] [135 111] [135 110] [134 110] [136 110]]}))
(def star-4 (polyline {:id 3024 :style star-style-4 :pts [[187 89] [187 91] [187 90] [186 90] [188 90]]}))
(def star-5 (polyline {:id 3025 :style star-style-5 :pts [[239 105] [239 107] [239 106] [238 106] [240 106]]}))
(def star-6 (polyline {:id 3026 :style star-style-6 :pts [[289 81] [289 83] [289 82] [288 82] [290 82]]}))

(def player-entity-id 3002)
(def player-collision-entity-id 3006)
(def obstacle-entity-id 3003)
(def melody-input-pin 1)
(def demo-melody-track-id :game-demo-melody)
(def demo-melody-steps
  [{:melody :G5 :backing [:D4] :dur :s}
   {:melody :Bb5 :backing [:F4] :dur :s}
   {:melody :D6 :backing [:G4] :dur :e}])
(def demo-melody-opts
  {:channel-count 2
   :melody-vol 220
   :backing-volumes [132]
   :tempo-bpm 168
   :gate-percent 78})

;; Piezo-friendly Star Wars title phrases used by the host/ESP demo.
(def starwars-title-steps
  [{:rest :t}
   {:melody :G5 :backing [:D4] :dur :q}
   {:melody :G5 :backing [:D4] :dur :q}
   {:melody :G5 :backing [:D4] :dur :q}
   {:melody :Eb5 :backing [:C4] :dur :de}
   {:melody :Bb5 :backing [:F4] :dur :s}
   {:melody :G5 :backing [:D4] :dur :q}

   {:melody :Eb5 :backing [:C4] :dur :de}
   {:melody :Bb5 :backing [:F4] :dur :s}
   {:melody :G5 :backing [:D4] :dur :q}
   {:rest :e}

   {:melody :D6 :backing [:A4] :dur :q}
   {:melody :D6 :backing [:A4] :dur :q}
   {:melody :D6 :backing [:A4] :dur :q}
   {:melody :Eb6 :backing [:Bb4] :dur :de}
   {:melody :Bb5 :backing [:F4] :dur :s}
   {:melody :Gb5 :backing [:Db4] :dur :q}

   {:melody :Eb5 :backing [:C4] :dur :de}
   {:melody :Bb5 :backing [:F4] :dur :s}
   {:melody :G5 :backing [:D4] :dur :q}
   {:rest :s}])

(def player-small-state (atom false))
(def deco-scene-state (atom nil))
(def score-scene-state (atom nil))
(def game-scene-state (atom nil))
(def demo-melody-trigger-count* (atom 0))
(def demo-input-watcher-id* (atom nil))
(def slot-descriptor-list
  [{:id :deco :atom deco-scene-state}
   {:id :score :atom score-scene-state}
   {:id :game :atom game-scene-state}])
(def collision-entity-id-pair [player-collision-entity-id obstacle-entity-id])

(defn play-starwars-title!
  "Convenience demo for host/ESP playback."
  []
  (sound/play-steps! :starwars-title-2v starwars-title-steps
                     {:channel-count 2
                      :melody-vol 220
                      :backing-volumes [195]
                      :tempo-bpm 100
                      :gate-percent 78}))

(defn slot-descriptors
  "Returns the canonical ordered slot descriptor vector for tiny-gfx runtime
and game-demo startup."
  []
  slot-descriptor-list)

(defn- make-player-tri
  [id style t]
  (tri (assoc player-geometry
         :id id
         :t t
         :style style)))

(defn- apply-player-scale
  [game-scene player-small?]
  (let [root (:root game-scene)
        player (get root player-entity-id)
        next-timeline (player-jump-timeline-for-state player-small?)]
    (if (= (:t player) next-timeline)
      game-scene
      (assoc game-scene :root
             (assoc root player-entity-id (assoc player :t next-timeline))))))

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
    (when (and (not= next-state :ignore)
               (not= next-state @player-small-state))
      (reset! player-small-state next-state)
      (let [game-scene @game-scene-state]
        (when game-scene
          (reset! game-scene-state (apply-player-scale game-scene next-state)))))
    nil))

(defn configure-collision-toggle-callback!
  "Configures the collision response callback used by the game demo."
  []
  (collision/set-collision-callback! on-player-collision-toggle!))

(defn on-demo-gpio-input!
  "Game-demo GPIO callback: a rising edge on the demo input pin triggers
a short melody from Clojure. Returns nil; host-side callback dispatch ignores
return values."
  [event]
  (let [pin (:pin event)
        value (:value event)]
    (when (and (= pin melody-input-pin)
               (= value 1))
      (swap! demo-melody-trigger-count* inc)
      (sound/play-steps! demo-melody-track-id demo-melody-steps demo-melody-opts))
    nil))

(defn configure-demo-input-watchers!
  "Registers the demo GPIO watcher used by the game demo input simulation."
  []
  (let [old-watcher-id @demo-input-watcher-id*]
    (when old-watcher-id
      (clojure.core/gpio-unwatch old-watcher-id))
    (reset! demo-input-watcher-id*
            (clojure.core/gpio-watch melody-input-pin on-demo-gpio-input!))
    nil))

(defn collision-entity-ids
  "Returns [player-collision-entity-id obstacle-entity-id] for game-demo collision state queries."
  []
  collision-entity-id-pair)

(def mountains (polyline {:id 1001 :style style-deco :pts mountain-pts}))
(def deco-root (group {:id root-id :style style-deco :children [1001]}))
(def deco-entities {root-id deco-root
                    1001 mountains})
(def deco-scene-template (frame-scene {:root deco-entities :clip-rect [0 184 320 56] :z 0}))

(def score-t (transform {:tx 8 :ty 21}))
(def score-text (vtext {:id 2001 :t score-t :style style-score :text score-text-timeline}))
(def score-root (group {:id root-id :style style-score :children [2001]}))
(def score-entities {root-id score-root
                     2001 score-text})
(def score-scene-template (frame-scene {:root score-entities :clip-rect [0 0 320 32] :z 1}))

(def hbar-timeline
  (timeline {:keyframes [[0 (transform {:tx -10 :ty 80 :rot 0})]
                         [1700 (transform {:tx 330 :ty 80 :rot 0})]]
             :loop true}))
(def game-terrain
  (polyline {:id 3001 :t terrain-timeline :style style-game-line
             :pts [[0 156] [60 156] [100 144] [150 156] [220 156] [260 146] [319 156]
                   [380 156] [420 146] [480 156] [560 156] [620 144]]}))
(def game-player
  (make-player-tri player-entity-id style-game-player player-jump-timeline))
(def game-player-collision
  (make-player-tri player-collision-entity-id style-player-collision-proxy player-jump-timeline))
(def game-rocket-body
  (polyline {:id obstacle-entity-id :t rocket-timeline :style style-rocket-body
             :pts rocket-body-pts :closed true}))
(def game-rocket-nose
  (tri (assoc rocket-nose-geometry
         :id 3005 :t rocket-timeline :style style-rocket-nose)))
(def game-caption-t (transform {:tx 96 :ty 52}))
(def game-caption (vtext {:id 3004 :t game-caption-t :style style-score :text "GAME SCENE"}))
(def game-hbar (tri {:id 3010 :t hbar-timeline :style style-hbar
                     :x1 0 :y1 -4 :x2 20 :y2 0 :x3 0 :y3 4}))
(def game-root
  (group {:id root-id :style style-score
          :children [3020 3001 player-collision-entity-id player-entity-id obstacle-entity-id 3005 3004 3010]}))
(def game-entities-static
  {root-id game-root
   3001 game-terrain
   obstacle-entity-id game-rocket-body
   3005 game-rocket-nose
   3004 game-caption
   3010 game-hbar
   3020 game-stars-group
   3021 star-1
   3022 star-2
   3023 star-3
   3024 star-4
   3025 star-5
   3026 star-6})
(def collision-rule
  (->SpatialRule :player-vs-rocket :game :collision obstacle-entity-id player-collision-entity-id 0 nil))
(def hearing-rule
  (->SpatialRule :enemy-hearing :game :proximity obstacle-entity-id player-collision-entity-id 24 :hearing))

(defn create-demo-bundle
  "Returns a vector with the demo frame-scenes used by the game demo.
Index layout:
0 deco-scene
1 score-scene
2 game-scene"
  []
  (let [game-player (make-player-tri player-entity-id style-game-player player-jump-timeline)
        game-player-collision (make-player-tri player-collision-entity-id
                                               style-player-collision-proxy
                                               player-jump-timeline)
        game-entities (assoc game-entities-static
                        player-collision-entity-id game-player-collision
                        player-entity-id game-player)
        game-scene (frame-scene {:root game-entities
                                 :clip-rect [0 40 320 136]
                                 :z 2
                                 :collision-rules [collision-rule hearing-rule]})
        demo-bundle [deco-scene-template score-scene-template game-scene]]
    (reset! player-small-state false)
    (reset! deco-scene-state deco-scene-template)
    (reset! score-scene-state score-scene-template)
    (reset! game-scene-state game-scene)
    (reset! demo-melody-trigger-count* 0)
    (configure-collision-toggle-callback!)
    (configure-demo-input-watchers!)
    demo-bundle))

)TINY_GFX_HOST"

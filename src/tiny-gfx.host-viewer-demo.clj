R"TINY_GFX_HOST(
(ns tiny-gfx.host-viewer-demo
  (:require [tiny-gfx.scene :refer [->Transform ->Style ->Group ->Polyline
                                     ->Tri ->VText ->FrameScene ->Timeline color]]))

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
  [[-12 3] [8 3] [8 -3] [-12 -3]
   [-16 -7] [-20 -7] [-20 -2] [-13 -2]
   [-13 -1] [-20 -1] [-20 1] [-13 1]
   [-13 2] [-20 2] [-20 7] [-16 7]])

(def terrain-timeline
  (timeline {:keyframes [[0 (transform {:tx 0 :ty 0 :rot 0})]
                         [2666 (transform {:tx -320 :ty 0 :rot 0})]]
             :loop true}))

(def player-jump-timeline
  (timeline {:keyframes [[0 (transform {:tx 0 :ty 0 :rot 0})]
                         [133 (transform {:tx 0 :ty -10 :rot 0})]
                         [266 (transform {:tx 0 :ty 0 :rot 0})]
                         [800 (transform {:tx 0 :ty 0 :rot 0})]]
             :loop true}))

(def rocket-timeline
  (timeline {:keyframes [[0 (transform {:tx 339 :ty 126 :rot -90})]
                         [3000 (transform {:tx -21 :ty 126 :rot -90})]]
             :loop true}))

(def star-drift-timeline
  (timeline {:keyframes [[0 (transform {:tx 0 :ty 0 :rot 0})]
                         [8000 (transform {:tx -320 :ty 0 :rot 0})]]
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
  (timeline {:keyframes [[0 style-star-white] [450 style-star-black]
                         [1100 style-star-yellow] [1650 style-star-black]
                         [2600 style-star-cyan] [3300 style-star-black]
                         [4200 style-star-yellow] [5200 style-star-black]
                         [6200 style-star-white]]
             :loop true}))

(def star-style-2
  (timeline {:keyframes [[0 style-star-cyan] [380 style-star-black]
                         [980 style-star-white] [1500 style-star-black]
                         [2300 style-star-yellow] [3000 style-star-black]
                         [3900 style-star-cyan] [4800 style-star-black]
                         [5900 style-star-white]]
             :loop true}))

(def star-style-3
  (timeline {:keyframes [[0 style-star-yellow] [520 style-star-black]
                         [1200 style-star-white] [1750 style-star-black]
                         [2550 style-star-cyan] [3400 style-star-black]
                         [4300 style-star-white] [5150 style-star-black]
                         [6100 style-star-yellow] [7000 style-star-white]]
             :loop true}))

(def star-style-4
  (timeline {:keyframes [[0 style-star-white] [600 style-star-black]
                         [1450 style-star-cyan] [2100 style-star-black]
                         [3150 style-star-yellow] [3900 style-star-black]
                         [5050 style-star-cyan] [5800 style-star-black]
                         [7000 style-star-white]]
             :loop true}))

(def star-style-5
  (timeline {:keyframes [[0 style-star-yellow] [470 style-star-black]
                         [1180 style-star-cyan] [1880 style-star-black]
                         [2800 style-star-white] [3550 style-star-black]
                         [4500 style-star-yellow] [5400 style-star-black]
                         [6500 style-star-cyan] [7400 style-star-white]]
             :loop true}))

(def star-style-6
  (timeline {:keyframes [[0 style-star-cyan] [530 style-star-black]
                         [1350 style-star-yellow] [1980 style-star-black]
                         [3050 style-star-white] [3820 style-star-black]
                         [4960 style-star-cyan] [5740 style-star-black]
                         [6880 style-star-yellow] [7800 style-star-white]]
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

(defn create-demo-bundle
  "Returns a vector with the demo frame-scenes used by the host viewer.
Index layout:
0 deco-scene
1 score-scene
2 game-scene"
  []
  (let [mountains (polyline {:id 1001 :style style-deco :pts mountain-pts})
        deco-root (group {:id root-id :style style-deco :children [1001]})
        deco-entities {root-id deco-root
                       1001 mountains}
        deco-scene (frame-scene {:root deco-entities :clip-rect [0 184 320 56] :z 0})]
    (let [score-t (transform {:tx 8 :ty 21})
          score-text (vtext {:id 2001 :t score-t :style style-score :text score-text-timeline})
          score-root (group {:id root-id :style style-score :children [2001]})
          score-entities {root-id score-root
                          2001 score-text}
          score-scene (frame-scene {:root score-entities :clip-rect [0 0 320 32] :z 1})]
      (let [hbar-timeline (timeline {:keyframes [[0 (transform {:tx -10 :ty 80 :rot 0})]
                                                 [1700 (transform {:tx 330 :ty 80 :rot 0})]]
                                     :loop true})
            game-terrain (polyline {:id 3001 :t terrain-timeline :style style-game-line
                                    :pts [[0 156] [60 156] [100 144] [150 156] [220 156] [260 146] [319 156]
                                          [380 156] [420 146] [480 156] [560 156] [620 144]]})
            game-player (tri {:id 3002 :t player-jump-timeline :style style-game-player
                              :x1 56 :y1 146 :x2 72 :y2 118 :x3 88 :y3 146})
            game-rocket-body (polyline {:id 3003 :t rocket-timeline :style style-rocket-body
                                        :pts rocket-body-pts :closed true})
            game-rocket-nose (tri {:id 3005 :t rocket-timeline :style style-rocket-nose
                                   :x1 20 :y1 0 :x2 10 :y2 -3 :x3 10 :y3 3})
            game-caption-t (transform {:tx 96 :ty 52})
            game-caption (vtext {:id 3004 :t game-caption-t :style style-score :text "GAME SCENE"})
            game-hbar (tri {:id 3010 :t hbar-timeline :style style-hbar
                            :x1 0 :y1 -4 :x2 20 :y2 0 :x3 0 :y3 4})
            game-root (group {:id root-id :style style-score
                              :children [3020 3001 3002 3003 3005 3004 3010]})
            game-entities {root-id game-root
                           3001 game-terrain
                           3002 game-player
                           3003 game-rocket-body
                           3005 game-rocket-nose
                           3004 game-caption
                           3010 game-hbar
                           3020 game-stars-group
                           3021 star-1
                           3022 star-2
                           3023 star-3
                           3024 star-4
                           3025 star-5
                           3026 star-6}
            game-scene (frame-scene {:root game-entities :clip-rect [0 40 320 136] :z 2})]
        [deco-scene
         score-scene
         game-scene]))))

)TINY_GFX_HOST"

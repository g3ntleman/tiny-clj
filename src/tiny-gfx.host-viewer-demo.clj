R"TINY_GFX_HOST(
(ns tiny-gfx.host-viewer-demo
  (:require [tiny-gfx.scene]))

;; Keep constructors local in this namespace for host-viewer bootstrap.
(defrecord Transform [tx ty sx sy rot])
(defrecord Style [stroke_rgb565 stroke_width visible has_fill fill_rgb565 has_bg_rgb565 bg_rgb565])
(defrecord Group [id t style visible children])
(defrecord Polyline [id t style visible pts closed])
(defrecord Tri [id t style visible x1 y1 x2 y2 x3 y3])
(defrecord VText [id t style visible x y scale rot text])
(defrecord FrameScene [root clip-rect z visible opaque erase-rgb565 guard-px collision-rules])

(defn style
  [{:keys [stroke-rgb565 stroke-width visible has-fill fill-rgb565 has-bg-rgb565 bg-rgb565]
    :or   {stroke-rgb565 0
           stroke-width 1
           visible true
           has-fill false
           fill-rgb565 0
           has-bg-rgb565 false
           bg-rgb565 0}}]
  (->Style stroke-rgb565 stroke-width visible has-fill fill-rgb565 has-bg-rgb565 bg-rgb565))

(defn transform
  [{:keys [tx ty sx sy rot]
    :or   {tx 0 ty 0 sx 1 sy 1 rot 0}}]
  (->Transform tx ty sx sy rot))

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
  [{:keys [root clip-rect z visible opaque erase-rgb565 guard-px collision-rules]
    :or   {clip-rect [0 0 320 240]
           z 0
           visible true
           opaque true
           erase-rgb565 0
           guard-px 1
           collision-rules nil}}]
  (->FrameScene root clip-rect z visible opaque erase-rgb565 guard-px collision-rules))

(def color-cyan 2047)
(def color-white 65535)
(def color-green 2016)
(def color-magenta 63519)
(def color-yellow 65504)
(def color-red 63488)

(def style-deco (style {:stroke-rgb565 color-cyan :stroke-width 2}))
(def style-score (style {:stroke-rgb565 color-white :stroke-width 1}))
(def style-game-line (style {:stroke-rgb565 color-green :stroke-width 2}))
(def style-game-player (style {:stroke-rgb565 color-magenta :stroke-width 3}))
(def style-rocket-body (style {:stroke-rgb565 color-yellow :stroke-width 2 :has-fill true :fill-rgb565 color-yellow}))
(def style-rocket-nose (style {:stroke-rgb565 color-red :stroke-width 2 :has-fill true :fill-rgb565 color-red}))
(def style-hbar (style {:stroke-rgb565 color-white :stroke-width 1 :has-fill true :fill-rgb565 color-white}))

(def mountain-pts
  [[0 228] [26 206] [58 226] [92 196] [126 225]
   [162 202] [198 230] [238 192] [276 226] [319 204]])

(def rocket-body-pts
  [[-12 3] [8 3] [8 -3] [-12 -3]
   [-16 -7] [-20 -7] [-20 -2] [-13 -2]
   [-13 -1] [-20 -1] [-20 1] [-13 1]
   [-13 2] [-20 2] [-20 7] [-16 7]])

(defn create-demo-bundle
  "Returns a vector with the demo frame-scenes and mutable handles used by the host viewer.
Index layout:
0 deco-scene
1 score-scene
2 game-scene
3 terrain-transform
4 player-transform
5 rocket-body-transform
6 rocket-nose-transform
7 hbar-transform
8 player-tri
9 score-text"
  []
  (let [mountains (polyline {:id 1001 :style style-deco :pts mountain-pts})
        deco-root (group {:id 1000 :style style-deco :children [mountains]})
        deco-scene (frame-scene {:root deco-root :clip-rect [0 184 320 56] :z 0})]
    (let [score-t (transform {:tx 8 :ty 21})
          score-text (vtext {:id 2001 :t score-t :style style-score :text "SCORE 0000    LIFES 3"})
          score-root (group {:id 2000 :style style-score :children [score-text]})
          score-scene (frame-scene {:root score-root :clip-rect [0 0 320 32] :z 1})]
      (let [terrain-t (transform {})
            player-t (transform {})
            rocket-body-t (transform {})
            rocket-nose-t (transform {})
            hbar-t (transform {})
            game-terrain (polyline {:id 3001 :t terrain-t :style style-game-line
                                    :pts [[0 156] [60 156] [100 144] [150 156] [220 156] [260 146] [319 156]
                                          [380 156] [420 146] [480 156] [560 156] [620 144]]})
            game-player (tri {:id 3002 :t player-t :style style-game-player
                              :x1 56 :y1 146 :x2 72 :y2 118 :x3 88 :y3 146})
            game-rocket-body (polyline {:id 3003 :t rocket-body-t :style style-rocket-body
                                        :pts rocket-body-pts :closed true})
            game-rocket-nose (tri {:id 3005 :t rocket-nose-t :style style-rocket-nose
                                   :x1 20 :y1 0 :x2 10 :y2 -3 :x3 10 :y3 3})
            game-caption-t (transform {:tx 96 :ty 52})
            game-caption (vtext {:id 3004 :t game-caption-t :style style-score :text "GAME SCENE"})
            game-hbar (tri {:id 3010 :t hbar-t :style style-hbar
                            :x1 0 :y1 -4 :x2 20 :y2 0 :x3 0 :y3 4})
            game-root (group {:id 3000 :style style-score
                              :children [game-terrain game-player game-rocket-body game-rocket-nose game-caption game-hbar]})
            game-scene (frame-scene {:root game-root :clip-rect [0 40 320 136] :z 2})]
        [deco-scene
         score-scene
         game-scene
         terrain-t
         player-t
         rocket-body-t
         rocket-nose-t
         hbar-t
         game-player
         score-text]))))

)TINY_GFX_HOST"

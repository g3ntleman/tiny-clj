R"TINY_FX_STARTUP(
(ns tiny-fx.startup
  (:require [tiny-fx.gfx :as gfx]
            [tiny-fx.gfx-scene :refer [->Transform ->Style ->Group ->Rect ->Polyline
                                       ->VText ->FrameScene ->Timeline color]]))

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
  (->Polyline id t style visible pts closed nil))

(defn rect
  [{:keys [id t style visible x y w h]
    :or   {t nil visible true x 0 y 0 w 0 h 0}}]
  (->Rect id t style visible x y w h nil))

(defn vtext
  [{:keys [id t style visible x y scale rot text]
    :or   {t nil visible true x 0 y 0 scale 1 rot 0 text ""}}]
  (->VText id t style visible x y scale rot text nil))

(defn group
  [{:keys [id t style visible children]
    :or   {t nil visible true children []}}]
  (->Group id t style visible children nil))

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

(def color-black (color 0x000000))
(def color-white (color 0xFFFFFF))
(def color-cyan (color 0x00FFFF))
(def color-yellow (color 0xFFFF00))
(def color-screen-green (color 0x9BBC0F))
(def color-screen-green-dark (color 0x306230))

(def style-title (style {:stroke-color color-screen-green :stroke-width 2}))
(def style-subtitle (style {:stroke-color color-screen-green :stroke-width 1}))
(def style-display-frame (style {:stroke-color color-screen-green-dark :stroke-width 2 :has-fill true :fill-color color-screen-green-dark}))
(def style-display-screen (style {:stroke-color color-black :stroke-width 1 :has-fill true :fill-color color-black}))
(def style-star-white (style {:stroke-color color-white :stroke-width 1 :has-fill true :fill-color color-white}))
(def style-star-black (style {:stroke-color color-black :stroke-width 1 :has-fill true :fill-color color-black}))
(def style-star-yellow (style {:stroke-color color-yellow :stroke-width 1 :has-fill true :fill-color color-yellow}))
(def style-star-cyan (style {:stroke-color color-cyan :stroke-width 1 :has-fill true :fill-color color-cyan}))

(def title-text-timeline
  (timeline {:keyframes [[0 ""]
                         [180 "TINYBOY"]
                         [3200 "TINYBOY"]]
             :loop false}))

(def subtitle-text-timeline
  (timeline {:keyframes [[0 ""]
                         [980 "ESP32 + TINY-CLJ"]
                         [3200 "ESP32 + TINY-CLJ"]]
             :loop false}))

(def title-timeline
  (timeline {:keyframes [[0 (transform {:tx -96 :ty -32 :sx 4.0 :sy 4.0 :rot -10})]
                         [260 (transform {:tx -18 :ty 8 :sx 3.1 :sy 3.1 :rot -6})]
                         [640 (transform {:tx 42 :ty 40 :sx 2.1 :sy 2.1 :rot -2})]
                         [1080 (transform {:tx 78 :ty 66 :sx 1.25 :sy 1.25 :rot 0})]
                         [1500 (transform {:tx 86 :ty 74 :sx 1.0 :sy 1.0 :rot 0})]
                         [3200 (transform {:tx 86 :ty 74 :sx 1.0 :sy 1.0 :rot 0})]]
             :loop false}))

(def subtitle-timeline
  (timeline {:keyframes [[0 (transform {:tx 56 :ty 214 :sx 2.0 :sy 2.0 :rot 0})]
                         [980 (transform {:tx 56 :ty 214 :sx 2.0 :sy 2.0 :rot 0})]
                         [1320 (transform {:tx 74 :ty 138 :sx 1.2 :sy 1.2 :rot 0})]
                         [1680 (transform {:tx 84 :ty 148 :sx 1.0 :sy 1.0 :rot 0})]
                         [3200 (transform {:tx 84 :ty 148 :sx 1.0 :sy 1.0 :rot 0})]]
             :loop false}))

(def stars-back-timeline
  (timeline {:keyframes [[0 (transform {:tx 0 :ty -260 :rot 0})]
                         [20000 (transform {:tx 0 :ty 260 :rot 0})]]
             :loop true}))

(def stars-front-timeline
  (timeline {:keyframes [[0 (transform {:tx 0 :ty -240 :rot 0})]
                         [14000 (transform {:tx 0 :ty 240 :rot 0})]]
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

(def stars-back
  (group {:id 1010 :t stars-back-timeline :style style-star-white
          :children [1011 1012 1013 1014 1015 1016 1017 1018 1019 1030 1031 1032 1033 1034 1035 1036]}))

(def stars-front
  (group {:id 1020 :t stars-front-timeline :style style-star-white
          :children [1021 1022 1023 1024 1025 1026 1027 1028 1029 1040 1041 1042 1043 1044 1045 1046]}))

(def star-1 (polyline {:id 1011 :style star-style-1 :pts [[28 34] [28 36] [28 35] [27 35] [29 35]]}))
(def star-2 (polyline {:id 1012 :style star-style-2 :pts [[76 58] [76 60] [76 59] [75 59] [77 59]]}))
(def star-3 (polyline {:id 1013 :style star-style-3 :pts [[138 28] [138 30] [138 29] [137 29] [139 29]]}))
(def star-4 (polyline {:id 1014 :style star-style-4 :pts [[214 44] [214 46] [214 45] [213 45] [215 45]]}))
(def star-5 (polyline {:id 1015 :style star-style-5 :pts [[272 24] [272 26] [272 25] [271 25] [273 25]]}))
(def star-6 (polyline {:id 1016 :style star-style-6 :pts [[304 62] [304 64] [304 63] [303 63] [305 63]]}))
(def star-13 (polyline {:id 1017 :style star-style-1 :pts [[18 86] [18 88] [18 87] [17 87] [19 87]]}))
(def star-14 (polyline {:id 1018 :style star-style-3 :pts [[58 122] [58 124] [58 123] [57 123] [59 123]]}))
(def star-15 (polyline {:id 1019 :style star-style-5 :pts [[118 94] [118 96] [118 95] [117 95] [119 95]]}))
(def star-16 (polyline {:id 1030 :style star-style-2 :pts [[166 70] [166 72] [166 71] [165 71] [167 71]]}))
(def star-17 (polyline {:id 1031 :style star-style-4 :pts [[198 118] [198 120] [198 119] [197 119] [199 119]]}))
(def star-18 (polyline {:id 1032 :style star-style-6 :pts [[246 86] [246 88] [246 87] [245 87] [247 87]]}))
(def star-19 (polyline {:id 1033 :style star-style-1 :pts [[292 108] [292 110] [292 109] [291 109] [293 109]]}))
(def star-20 (polyline {:id 1034 :style star-style-3 :pts [[322 140] [322 142] [322 141] [321 141] [323 141]]}))
(def star-21 (polyline {:id 1035 :style star-style-5 :pts [[88 154] [88 156] [88 155] [87 155] [89 155]]}))
(def star-22 (polyline {:id 1036 :style star-style-2 :pts [[152 166] [152 168] [152 167] [151 167] [153 167]]}))
(def star-7 (polyline {:id 1021 :style star-style-4 :pts [[44 172] [44 174] [44 173] [43 173] [45 173]]}))
(def star-8 (polyline {:id 1022 :style star-style-2 :pts [[96 198] [96 200] [96 199] [95 199] [97 199]]}))
(def star-9 (polyline {:id 1023 :style star-style-6 :pts [[166 184] [166 186] [166 185] [165 185] [167 185]]}))
(def star-10 (polyline {:id 1024 :style star-style-1 :pts [[226 208] [226 210] [226 209] [225 209] [227 209]]}))
(def star-11 (polyline {:id 1025 :style star-style-3 :pts [[282 188] [282 190] [282 189] [281 189] [283 189]]}))
(def star-12 (polyline {:id 1026 :style star-style-5 :pts [[316 170] [316 172] [316 171] [315 171] [317 171]]}))
(def star-23 (polyline {:id 1027 :style star-style-6 :pts [[26 214] [26 216] [26 215] [25 215] [27 215]]}))
(def star-24 (polyline {:id 1028 :style star-style-2 :pts [[74 234] [74 236] [74 235] [73 235] [75 235]]}))
(def star-25 (polyline {:id 1029 :style star-style-4 :pts [[132 222] [132 224] [132 223] [131 223] [133 223]]}))
(def star-26 (polyline {:id 1040 :style star-style-1 :pts [[190 246] [190 248] [190 247] [189 247] [191 247]]}))
(def star-27 (polyline {:id 1041 :style star-style-3 :pts [[244 226] [244 228] [244 227] [243 227] [245 227]]}))
(def star-28 (polyline {:id 1042 :style star-style-5 :pts [[286 238] [286 240] [286 239] [285 239] [287 239]]}))
(def star-29 (polyline {:id 1043 :style star-style-6 :pts [[308 214] [308 216] [308 215] [307 215] [309 215]]}))
(def star-30 (polyline {:id 1044 :style star-style-2 :pts [[54 262] [54 264] [54 263] [53 263] [55 263]]}))
(def star-31 (polyline {:id 1045 :style star-style-4 :pts [[154 278] [154 280] [154 279] [153 279] [155 279]]}))
(def star-32 (polyline {:id 1046 :style star-style-1 :pts [[264 286] [264 288] [264 287] [263 287] [265 287]]}))

(def root-id 'root)

(def deco-root
  (group {:id root-id :style style-star-white :children [1010 1020]}))

(def deco-entities
  {root-id deco-root
   1010 stars-back
   1011 star-1
   1012 star-2
   1013 star-3
   1014 star-4
   1015 star-5
   1016 star-6
   1017 star-13
   1018 star-14
   1019 star-15
   1020 stars-front
   1021 star-7
   1022 star-8
   1023 star-9
   1024 star-10
   1025 star-11
   1026 star-12
   1027 star-23
   1028 star-24
   1029 star-25
   1030 star-16
   1031 star-17
   1032 star-18
   1033 star-19
   1034 star-20
   1035 star-21
   1036 star-22
   1040 star-26
   1041 star-27
   1042 star-28
   1043 star-29
   1044 star-30
   1045 star-31
   1046 star-32})

(def deco-scene-template
  (frame-scene {:root deco-entities :clip-rect [0 0 320 240] :z 0 :erase-color color-black}))

(def title-node
  (vtext {:id 2001 :t title-timeline :style style-title :x 0 :y 0 :scale 2 :text title-text-timeline}))

(def subtitle-node
  (vtext {:id 2002 :t subtitle-timeline :style style-subtitle :x 0 :y 0 :scale 1 :text subtitle-text-timeline}))

(def overlay-root
  (group {:id root-id :style style-title :children [1100 1101 2001 2002]}))

(def display-frame
  (rect {:id 1100 :style style-display-frame :x 58 :y 54 :w 204 :h 112}))

(def display-screen
  (rect {:id 1101 :style style-display-screen :x 70 :y 66 :w 180 :h 88}))

(def overlay-entities
  {root-id overlay-root
   1100 display-frame
   1101 display-screen
   2001 title-node
   2002 subtitle-node})

(def overlay-scene-template
  (frame-scene {:root overlay-entities :clip-rect [0 0 320 240] :z 3 :opaque false}))

(def deco-scene-state (atom nil))
(def overlay-scene-state (atom nil))

(def slot-descriptors
  [{:id :deco :atom deco-scene-state}
   {:id :score :atom overlay-scene-state}])

(defn create-startup-bundle
  "Creates the global FX startup bundle and publishes fresh scene atoms."
  []
  (let [bundle [deco-scene-template overlay-scene-template]]
    (reset! deco-scene-state deco-scene-template)
    (reset! overlay-scene-state overlay-scene-template)
    bundle))

(defn start!
  "Creates the startup scenes and starts the runtime renderer with them."
  []
  (let [bundle (create-startup-bundle)
        started? (gfx/start-renderer! slot-descriptors)]
    {:bundle bundle
     :slots slot-descriptors
     :started? started?}))
)TINY_FX_STARTUP"

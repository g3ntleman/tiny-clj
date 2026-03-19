(ns tiny-clj.deployment)

(def breakout-host-state* (atom nil))
(def breakout-host-scene* (atom nil))

(defn- breakout-init-state
  []
  (require 'tiny-breakout.core)
  (tiny-breakout.core/init-state))

(defn- breakout-scene-record
  [state]
  (require 'tiny-breakout.scene)
  (record-from-map 'FrameScene (dissoc (tiny-breakout.scene/build-scene state) :type)))

(defn breakout-host-apply-input!
  "Applies one discrete breakout input event to host state and scene."
  [input]
  (require 'tiny-breakout.input)
  (require 'tiny-breakout.core)
  (let [intent (tiny-breakout.input/normalize-paddle-intent input)
        next-state (swap! breakout-host-state*
                          (fn [state]
                            (tiny-breakout.core/step-state state intent 16)))]
    (reset! breakout-host-scene* (breakout-scene-record next-state)))
  nil)

(defn breakout-host-button-event!
  "Translates one semantic button event into a discrete breakout input mutation."
  [event]
  (let [kind (:kind event)
        id (:id event)]
    (if (= kind :button/down)
      (case id
        :left (breakout-host-apply-input! {:left true})
        :right (breakout-host-apply-input! {:right true})
        :fire (breakout-host-apply-input! {:launch true})
        :y (breakout-host-apply-input! {:pause true})
        nil)
      nil)))

(defn breakout-host-spatial-callback!
  [event]
  (require 'tiny-fx.gfx-collision)
  (tiny-fx.gfx-collision/invoke-collision-callback! event)
  nil)

(defn breakout-host-config
  "Builds host-viewer config for the breakout demo with semantic button input."
  []
  (let [state (breakout-init-state)
        frame (breakout-scene-record state)]
    (reset! breakout-host-state* state)
    (reset! breakout-host-scene* frame)
    {:slots [{:id :game :atom breakout-host-scene*}]
     :host-runtime :native-breakout
     :spatial-callback breakout-host-spatial-callback!
     :game-state-atom breakout-host-state*
     :game-scene-atom breakout-host-scene*
     :entry :tiny-breakout}))

(defn breakout-demo-config
  "Builds deterministic startup config for the breakout demo."
  []
  (require 'tiny-breakout.scene)
  (let [state (breakout-init-state)
        frame (tiny-breakout.scene/build-scene state)]
    {:state state
     :frame frame
     :entry :tiny-breakout}))

(ns tiny-clj.deployment
  (:require [tiny-breakout.core :as breakout-core]
            [tiny-breakout.input :as breakout-input]
            [tiny-breakout.scene :as breakout-scene]
            [tiny-clj.event :as event]))

(def breakout-host-state* (atom nil))
(def breakout-host-scene* (atom nil))

(defn- breakout-scene-record
  [state]
  (record-from-map 'FrameScene (dissoc (breakout-scene/build-scene state) :type)))

(defn- breakout-host-release-controls!
  []
  (event/on {:source :button :id :left} nil)
  (event/on {:source :button :id :right} nil)
  (event/on {:source :button :id :fire} nil)
  (event/on {:source :button :id :y} nil)
  nil)

(defn breakout-host-apply-input!
  "Applies one discrete breakout input event to host state and scene."
  [input]
  (let [intent (breakout-input/normalize-paddle-intent input)
        next-state (swap! breakout-host-state*
                          (fn [state]
                            (breakout-core/step-state state intent 16)))]
    (reset! breakout-host-scene* (breakout-scene-record next-state)))
  nil)

(defn breakout-host-button-event!
  "Translates one semantic button event into a discrete breakout input mutation."
  [event]
  (let [kind (:kind event)
        id (:id event)]
    (cond
      (and (= kind :button/down) (= id :left))
      (breakout-host-apply-input! {:left true})

      (and (= kind :button/down) (= id :right))
      (breakout-host-apply-input! {:right true})

      (and (= kind :button/down) (= id :fire))
      (breakout-host-apply-input! {:launch true})

      (and (= kind :button/down) (= id :y))
      (breakout-host-apply-input! {:pause true})

      :else nil)))

(defn- breakout-host-bind-controls!
  []
  (breakout-host-release-controls!)
  (event/on :button :left breakout-host-button-event!)
  (event/on :button :right breakout-host-button-event!)
  (event/on :button :fire breakout-host-button-event!)
  (event/on :button :y breakout-host-button-event!)
  nil)

(defn breakout-host-spatial-callback!
  [_event]
  nil)

(defn breakout-host-animation-event!
  [_event]
  nil)

(defn breakout-host-config
  "Builds host-viewer config for the breakout demo with semantic button input."
  []
  (let [state (breakout-core/init-state)
        frame (breakout-scene-record state)]
    (reset! breakout-host-state* state)
    (reset! breakout-host-scene* frame)
    (breakout-host-release-controls!)
    {:slots [{:id :game :atom breakout-host-scene*}]
     :host-runtime :native-breakout
     :spatial-callback breakout-host-spatial-callback!
     :game-state-atom breakout-host-state*
     :game-scene-atom breakout-host-scene*
     :entry :tiny-breakout}))

(defn breakout-demo-config
  "Builds deterministic startup config for the breakout demo."
  []
  (let [state (breakout-core/init-state)
        frame (breakout-scene/build-scene state)]
    {:state state
     :frame frame
     :entry :tiny-breakout}))

(ns tiny-breakout.runtime
  (:require [tiny-breakout.core :as core]
            [tiny-breakout.scene :as scene]
            [tiny-fx.gfx-collision :as collision]
            [tiny-clj.event :as event]
            [tiny-clj.runtime :as runtime]))

(def state* (atom nil))
(def scene* (atom nil))

(def ^:private segment-timer-id :tiny-breakout/segment-end)

(defn- scene-record
  [state]
  (record-from-map 'FrameScene (dissoc (scene/build-scene state) :type)))

(defn- rendered-ball-position
  []
  (let [rendered (runtime/renderer-state :game 1003)]
    (if (and (map? rendered)
             (number? (:tx rendered))
             (number? (:ty rendered)))
      {:x (:tx rendered)
       :y (:ty rendered)}
      nil)))

(defn- sync-segment-timer!
  [state]
  (cancel-timer segment-timer-id)
  (let [segment (:ball-segment state)]
    (when (and (map? segment)
               (number? (:id segment))
               (number? (:end-ms segment)))
      (let [segment-id (:id segment)
            delay-ms (- (:end-ms segment) (current-time-ms))
            safe-delay (if (> delay-ms 0) delay-ms 0)]
        (schedule safe-delay
                  {:id segment-timer-id
                   :fn (fn []
                         (let [next-state (swap! state*
                                                 (fn [current]
                                                   (core/apply-segment-end current segment-id)))]
                           (reset! scene* (scene-record next-state))
                           (sync-segment-timer! next-state)
                           nil))}))))
  nil)

(defn- publish-state!
  [state]
  (reset! state* state)
  (reset! scene* (scene-record state))
  (sync-segment-timer! state)
  nil)

(defn- state-after-input
  [input-map]
  (let [now-ms (current-time-ms)
        current @state*
        rendered (if (or (= (:phase current) :play)
                         (= (:phase current) :pause))
                   (rendered-ball-position)
                   nil)]
    (core/apply-input current input-map now-ms rendered)))

(defn- normalize-paddle-intent
  [input]
  (let [left? (= true (get input :left))
        right? (= true (get input :right))
        rotary (let [v (get input :rotary-delta)]
                 (if (number? v) v 0))
        digital-dx (cond
                     (and left? (not right?)) -1
                     (and right? (not left?)) 1
                     :else 0)
        dx (+ digital-dx rotary)
        launch? (= true (get input :launch))
        pause? (= true (get input :pause))]
    {:dx (max -8 (min 8 dx))
     :launch? launch?
     :pause? pause?}))

(defn apply-input!
  [input-map]
  (let [intent (normalize-paddle-intent input-map)
        next-state (state-after-input intent)]
    (publish-state! next-state)))

(defn on-spatial-event!
  [event]
  (let [now-ms (current-time-ms)
        next-state (swap! state*
                          (fn [current]
                            (core/apply-spatial-event current event now-ms)))]
    (reset! scene* (scene-record next-state))
    (sync-segment-timer! next-state)
    (collision/invoke-collision-callback! event)
    nil))

(defn configure-input-watchers!
  []
  (event/on {:source :button :id :left}
            (fn [event]
              (when (= (:kind event) :button/down)
                (apply-input! {:left true}))
              nil))
  (event/on {:source :button :id :right}
            (fn [event]
              (when (= (:kind event) :button/down)
                (apply-input! {:right true}))
              nil))
  (event/on {:source :button :id :fire}
            (fn [event]
              (when (= (:kind event) :button/down)
                (apply-input! {:launch true}))
              nil))
  (event/on {:source :button :id :y}
            (fn [event]
              (when (= (:kind event) :button/down)
                (apply-input! {:pause true}))
              nil))
  nil)

(defn reset-runtime!
  []
  (publish-state! (core/init-state))
  (configure-input-watchers!)
  nil)

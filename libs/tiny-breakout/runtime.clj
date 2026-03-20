(ns tiny-breakout.runtime
  (:require [tiny-breakout.audio :as audio]
            [tiny-breakout.core :as core]
            [tiny-breakout.scene :as scene]
            [tiny-fx.gfx-collision :as collision]
            [tiny-clj.event :as event]
            [tiny-clj.runtime :as runtime]))

(def state* (atom nil))
(def scene* (atom nil))
(def ^:private idle-held-buttons {:left false :right false :last nil})
(def ^:private held-buttons* (atom idle-held-buttons))

(def ^:private segment-timer-id :tiny-breakout/segment-end)

(defn- button-down-event?
  [event]
  (and (= 0 (get event :value))
       (nil? (get event :pressed-ms))
       (nil? (get event :held-ms))))

(defn- button-release-event?
  [event]
  (and (= 1 (get event :value))
       (number? (get event :pressed-ms))
       (nil? (get event :held-ms))))

(defn- scene-record
  [state]
  (record-from-map 'FrameScene (dissoc (scene/build-scene state) :type)))

(defn- rendered-entity-state
  [entity-id]
  (runtime/renderer-state :game entity-id))

(defn- rendered-ball-position
  []
  (let [rendered (rendered-entity-state 1003)]
    (if (and (map? rendered)
             (number? (:tx rendered))
             (number? (:ty rendered)))
      {:x (:tx rendered)
       :y (:ty rendered)}
      nil)))

(defn- rendered-paddle-x
  []
  (let [rendered (rendered-entity-state 1002)]
    (if (and (map? rendered)
             (number? (:tx rendered)))
      (:tx rendered)
      nil)))

(defn- clamp-paddle-x
  [x]
  (let [right-limit (- core/playfield-width core/paddle-width)]
    (max 0 (min right-limit x))))

(defn- attached-ball-x
  [paddle-x]
  (+ paddle-x (quot core/paddle-width 2)))

(defn- attached-ball-y
  []
  (- core/paddle-y 6))

(defn- stop-paddle-motion
  [state]
  (assoc state :paddle-motion nil))

(defn- paddle-motion-x
  [state now-ms]
  (let [motion (:paddle-motion state)
        start-ms (get motion :start-ms)
        end-ms (get motion :end-ms)
        from-x (clamp-paddle-x (let [v (:paddle-x state)] (if (number? v) v 0)))
        to-x (clamp-paddle-x (let [v (:to-x motion)] (if (number? v) v from-x)))]
    (if (and (map? motion)
             (number? start-ms)
             (number? end-ms)
             (> end-ms start-ms))
      (let [clamped-now (cond
                          (< now-ms start-ms) start-ms
                          (> now-ms end-ms) end-ms
                          :else now-ms)
            elapsed-ms (- clamped-now start-ms)
            duration-ms (- end-ms start-ms)]
        (+ from-x (quot (* (- to-x from-x) elapsed-ms) duration-ms)))
      from-x)))

(defn- sync-paddle-state
  [state now-ms]
  (let [motion (:paddle-motion state)
        resolved-x (let [rendered-x (rendered-paddle-x)]
                     (if (number? rendered-x)
                       (clamp-paddle-x rendered-x)
                       (paddle-motion-x state now-ms)))
        phase (:phase state)
        state2 (assoc state :paddle-x resolved-x)
        state3 (if (or (= phase :title) (= phase :serve))
                 (assoc state2
                        :ball-x (attached-ball-x resolved-x)
                        :ball-y (attached-ball-y))
                 state2)]
    (if (and (map? motion)
             (number? (:end-ms motion))
             (>= now-ms (:end-ms motion)))
      (stop-paddle-motion state3)
      state3)))

(defn- paddle-motion-duration-ms
  [distance]
  (if (<= distance 0)
    0
    (quot (+ (* distance core/segment-step-ms)
             (- core/paddle-speed 1))
          core/paddle-speed)))

(defn- paddle-motion-direction
  [state]
  (let [motion (:paddle-motion state)
        dir (:dir motion)]
    (if (number? dir) dir nil)))

(defn- plan-paddle-motion
  [state dir now-ms]
  (let [from-x (clamp-paddle-x (let [v (:paddle-x state)] (if (number? v) v 0)))
        to-x (if (< dir 0)
               0
               (- core/playfield-width core/paddle-width))
        distance (abs (- to-x from-x))
        duration-ms (paddle-motion-duration-ms distance)]
    (assoc state
           :paddle-x from-x
           :paddle-motion (if (> duration-ms 0)
                            {:dir dir
                             :start-ms now-ms
                             :end-ms (+ now-ms duration-ms)
                             :to-x to-x}
                            nil))))

(defn- desired-paddle-direction
  [held]
  (let [left? (= true (get held :left))
        right? (= true (get held :right))
        last (get held :last)]
    (cond
      (and left? right? (= last :left)) -1
      (and left? right? (= last :right)) 1
      left? -1
      right? 1
      :else nil)))

(defn- apply-held-paddle-direction!
  []
  (let [now-ms (current-time-ms)
        current (sync-paddle-state @state* now-ms)
        desired-dir (desired-paddle-direction @held-buttons*)
        current-dir (paddle-motion-direction current)]
    (cond
      (nil? desired-dir)
      (publish-state! (stop-paddle-motion current))

      (= desired-dir current-dir)
      nil

      :else
      (publish-state! (plan-paddle-motion (stop-paddle-motion current)
                                          desired-dir
                                          now-ms)))))

(defn- update-held-button!
  [button-id pressed?]
  (swap! held-buttons*
         (fn [held]
           (assoc held
                  button-id pressed?
                  :last (if pressed? button-id (get held :last)))))
  (apply-held-paddle-direction!))

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
                         (let [now-ms (current-time-ms)
                               next-state (swap! state*
                                                 (fn [current]
                                                   (core/apply-segment-end-at-ms current segment-id now-ms)))]
                           (reset! scene* (scene-record next-state))
                           (sync-segment-timer! next-state)
                           nil))}))))
  nil)

(defn- publish-state!
  [state]
  (let [events (:events state)
        state-without-events (assoc state :events [])]
    (reset! state* state-without-events)
    (reset! scene* (scene-record state-without-events))
    (sync-segment-timer! state-without-events)
    (audio/play-events! events))
  nil)

(defn- state-after-input
  [input-map]
  (let [now-ms (current-time-ms)
        current (sync-paddle-state @state* now-ms)
        current (if (not= 0 (get input-map :dx))
                  (stop-paddle-motion current)
                  current)
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
    (publish-state! next-state)
    (collision/invoke-collision-callback! event)
    nil))

(defn- on-movement-button-event!
  [button-id event]
  (cond
    (button-down-event? event)
    (update-held-button! button-id true)

    (button-release-event? event)
    (update-held-button! button-id false)

    :else nil))

(defn- on-action-button-event!
  [input-map event]
  (when (button-down-event? event)
    (apply-input! input-map))
  nil)

(defn configure-input-watchers!
  []
  (event/on {:source :button :id :left}
            (fn [event]
              (on-movement-button-event! :left event)))
  (event/on {:source :button :id :right}
            (fn [event]
              (on-movement-button-event! :right event)))
  (event/on {:source :button :id :fire}
            (fn [event]
              (on-action-button-event! {:launch true} event)))
  (event/on {:source :button :id :y}
            (fn [event]
              (on-action-button-event! {:pause true} event)))
  nil)

(defn reset-runtime!
  []
  (reset! held-buttons* idle-held-buttons)
  (audio/preload!)
  (publish-state! (core/init-state))
  (configure-input-watchers!)
  nil)

(ns tiny-breakout.runtime
  (:require [tiny-breakout.core :as core]
            [tiny-breakout.audio :as audio]
            [tiny-breakout.scene :as scene]
            [tiny-clj.event :as event]
            [tiny-clj.runtime :as runtime]
            [tiny-fx.gfx-collision :as gfx-collision]
            [tiny-fx.sound-demos :as sound-demos]))

(def state* (atom nil))
(def scene* (atom nil))
(def ^:private idle-overlay-animation {:text "" :start-ms 0})
(def ^:private overlay-animation* (atom idle-overlay-animation))
(def idle-held-buttons {:left false :right false :last nil})
(def held-buttons* (atom idle-held-buttons))
;; Audio warmup status is kept in runtime state (:audio-prewarmed?) so reset/start
;; flows can stay explicit and data-driven.

(def ^:private segment-watch-id :tiny-breakout/segment-end)
(def ^:private segment-progress-source {:slot :game
                                        :entity-id :ball
                                        :field :x})
(def ^:private segment-watch-active* (atom false))
(def ^:private segment-watch-segment-id* (atom nil))
(def ^:private segment-fallback-timer-id :tiny-breakout/segment-end-fallback)

(defn- state-audio-prewarmed?
  [state]
  (= true (:audio-prewarmed? state)))

(defn- ensure-audio-prewarmed!
  []
  (when-not (state-audio-prewarmed? @state*)
    (audio/prewarm-engine!)
    (when (map? @state*)
      (swap! state* assoc :audio-prewarmed? true)))
  nil)

(defn- button-down-event?
  [event]
  (let [kind (get event :kind)]
    (or (= :button/down kind)
        (and (= 0 (get event :value))
             (nil? (get event :pressed-ms))
             (nil? (get event :held-ms))))))

(defn- button-release-event?
  [event]
  (let [kind (get event :kind)]
    (or (= :button/up kind)
        (and (not= :button/click kind)
             (= 1 (get event :value))
             (number? (get event :pressed-ms))
             (nil? (get event :held-ms))))))

(defn- overlay-animation-for-state
  [state now-ms]
  (let [overlay (scene/overlay-text (:phase state))]
    (if (= overlay "")
      idle-overlay-animation
      {:text overlay
       :start-ms now-ms})))

(defn- next-overlay-animation
  [current-animation previous-state next-state now-ms]
  (let [previous-overlay (scene/overlay-text (:phase previous-state))
        next-overlay (scene/overlay-text (:phase next-state))]
    (cond
      (= next-overlay "")
      idle-overlay-animation

      (not= previous-overlay next-overlay)
      {:text next-overlay
       :start-ms now-ms}

      :else
      current-animation)))

(defn- scene-state-with-overlay-animation
  [state animation]
  (let [overlay (scene/overlay-text (:phase state))]
    (if (and (not= overlay "")
             (= overlay (:text animation)))
      (assoc state :overlay-start-ms (:start-ms animation))
      (dissoc state :overlay-start-ms))))

(defn- scene-record-for-animation
  [state animation]
  (record-from-map 'FrameScene
                   (dissoc (scene/build-scene (scene-state-with-overlay-animation state animation))
                           :type)))

(defn- scene-record
  [state]
  (scene-record-for-animation state @overlay-animation*))

(defn- restart-overlay-animation!
  []
  (let [state @state*
        now-ms (current-time-ms)
        animation (overlay-animation-for-state state now-ms)]
    (reset! overlay-animation* animation)
    (when (map? state)
      (reset! scene* (scene-record-for-animation state animation))))
  nil)

(defn- on-segment-timeline-event!
  [event]
  (let [now-ms (current-time-ms)
        current @state*
        segment (:ball-segment current)
        segment-id (:id segment)
        progress (if (map? event) (:progress event) nil)
        at-end? (and (map? progress)
                     (= true (:at-end progress))
                     (= true (:end-event progress)))
        end-ms (:end-ms segment)
        ready? (or at-end?
                   (and (number? end-ms)
                        (<= end-ms now-ms)))
        resume-ms (if (and at-end? (number? end-ms))
                    end-ms
                    now-ms)]
    (when (and (map? segment)
               (number? segment-id)
               ready?)
      (let [latest @state*
            latest-segment (:ball-segment latest)]
        (when (and (map? latest-segment)
                   (= segment-id (:id latest-segment)))
          (publish-state!
           (core/apply-segment-end-at-ms latest segment-id resume-ms))))))
  nil)

(defn- rendered-entity-state
  [entity-id]
  (runtime/renderer-state :game entity-id))

(defn- rendered-ball-position
  []
  (let [rendered (rendered-entity-state :ball)]
    (if (and (map? rendered)
             (number? (:tx rendered))
             (number? (:ty rendered)))
      {:x (:tx rendered)
       :y (:ty rendered)}
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
        resolved-x (paddle-motion-x state now-ms)
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

(defn- next-held-buttons
  [held button-id pressed?]
  (assoc held
         button-id pressed?
         :last (if pressed? button-id (get held :last))))

(defn- next-state-for-held-paddle-direction
  [state held now-ms]
  (let [current (sync-paddle-state state now-ms)
        desired-dir (desired-paddle-direction held)
        current-dir (paddle-motion-direction current)]
    (cond
      (nil? desired-dir)
      (stop-paddle-motion current)

      (= desired-dir current-dir)
      nil

      :else
      (plan-paddle-motion (stop-paddle-motion current)
                          desired-dir
                          now-ms))))

(defn- apply-held-paddle-direction!
  [held]
  (let [now-ms (current-time-ms)
        next-state (next-state-for-held-paddle-direction @state* held now-ms)]
    (when (map? next-state)
      (publish-state! next-state))))

(defn- update-held-button!
  [button-id pressed?]
  (let [held (swap! held-buttons* next-held-buttons button-id pressed?)]
    (apply-held-paddle-direction! held)))

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

(defn- on-launch-button-event!
  [event]
  (when (button-down-event? event)
    (apply-input! {:launch true}))
  nil)

(defn- on-segment-fallback-timer!
  []
  (let [now-ms (current-time-ms)
        current @state*
        segment (:ball-segment current)
        segment-id (if (map? segment) (:id segment) nil)
        end-ms (if (map? segment) (:end-ms segment) nil)]
    (if (and (map? segment)
             (number? segment-id)
             (number? end-ms))
      (if (<= end-ms now-ms)
        (publish-state! (core/apply-segment-end-at-ms current segment-id end-ms))
        (schedule (max 1 (- end-ms now-ms))
                  {:id segment-fallback-timer-id
                   :fn on-segment-fallback-timer!}))
      (cancel-timer segment-fallback-timer-id)))
  nil)

(defn- dispatch-segment-progress-sample!
  []
  (when @segment-watch-active*
    (let [progress (runtime/renderer-timeline-progress
                    (:slot segment-progress-source)
                    (:entity-id segment-progress-source)
                    (:field segment-progress-source))]
      (when (map? progress)
        (event/dispatch-timeline-progress! progress))))
  nil)

(defn- publish-state-plan
  [previous-state next-state current-overlay-animation segment-watch-active? segment-watch-segment-id now-ms]
  (let [state (scene/with-expanded-collision-rules next-state)
        events (:events state)
        state-without-events (assoc state :events [])
        segment (:ball-segment state-without-events)
        segment-id (if (map? segment) (:id segment) nil)
        end-ms (if (map? segment) (:end-ms segment) nil)
        install-segment-watch? (and (map? segment)
                                    (not segment-watch-active?))
        active-after-install? (or segment-watch-active?
                                  install-segment-watch?)
        next-segment-watch-id (if (and active-after-install?
                                       (number? segment-id))
                                segment-id
                                nil)
        rearm-segment-watch? (and active-after-install?
                                  (number? segment-id)
                                  (not= segment-id segment-watch-segment-id))
        fallback-delay-ms (if (and (number? segment-id)
                                   (number? end-ms))
                            (max 1 (- end-ms now-ms))
                            nil)]
    {:state state-without-events
     :events events
     :overlay-animation (next-overlay-animation current-overlay-animation
                                                previous-state
                                                state-without-events
                                                now-ms)
     :install-segment-watch? install-segment-watch?
     :next-segment-watch-id next-segment-watch-id
     :rearm-segment-watch? rearm-segment-watch?
     :fallback-delay-ms fallback-delay-ms
     :dispatch-progress? (map? segment)}))

(defn- apply-publish-state-plan!
  [plan]
  (let [state-without-events (:state plan)]
    (reset! overlay-animation* (:overlay-animation plan))
    (when (:install-segment-watch? plan)
      (event/on {:source :timeline :id segment-watch-id}
                on-segment-timeline-event!)
      (reset! segment-watch-active* true))
    (if (:rearm-segment-watch? plan)
      (event/rearm-timeline-watch-edge! segment-watch-id)
      nil)
    (reset! segment-watch-segment-id* (:next-segment-watch-id plan))
    (if (number? (:fallback-delay-ms plan))
      (schedule (:fallback-delay-ms plan)
                {:id segment-fallback-timer-id
                 :fn on-segment-fallback-timer!})
      (cancel-timer segment-fallback-timer-id))
    (reset! state* state-without-events)
    (when (:dispatch-progress? plan)
      (dispatch-segment-progress-sample!))
    (when (seq (:events plan))
      (audio/play-events! (:events plan)))
    state-without-events))

(defn- publish-state-core!
  "Updates state atom, plays audio, manages segment watchers. Returns
  state-without-events for optional immediate scene rebuild by callers."
  [state]
  (let [plan (publish-state-plan @state*
                                 state
                                 @overlay-animation*
                                 @segment-watch-active*
                                 @segment-watch-segment-id*
                                 (current-time-ms))]
    (apply-publish-state-plan! plan)))

(defn publish-state!
  "Full state publish with immediate scene rebuild."
  [state]
  (let [s (publish-state-core! state)]
    (reset! scene* (scene-record s)))
  nil)

(defn apply-input!
  [input-map]
  (let [intent (normalize-paddle-intent input-map)
        next-state (state-after-input intent)]
    (publish-state! next-state)))

(defn on-spatial-event!
  "Handles one spatial collision event from the render thread.
  Paddle-only since brick collisions are resolved predictively by fx/sweep-aabb."
  [event]
  (let [now-ms (current-time-ms)
        next-state (core/apply-spatial-event @state* event now-ms)]
    (publish-state! next-state)
    (gfx-collision/dispatch-spatial-watchers! event))
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
              (on-launch-button-event! event)))
  (event/on {:source :button :id :y}
            (fn [event]
              (on-action-button-event! {:pause true} event)))
  nil)

(defn- deactivate-segment-watch!
  []
  (cancel-timer segment-fallback-timer-id)
  (reset! segment-watch-segment-id* nil)
  (when @segment-watch-active*
    (event/on {:source :timeline :id segment-watch-id} nil)
    (reset! segment-watch-active* false))
  nil)

(defn- install-initial-state!
  [state play-events?]
  (let [now-ms (current-time-ms)
        events (:events state)
        state-without-events (assoc state :events [])
        animation (overlay-animation-for-state state-without-events now-ms)]
    (reset! overlay-animation* animation)
    (reset! state* state-without-events)
    (reset! scene* (scene-record-for-animation state-without-events animation))
    (when (and play-events? (seq events))
      (audio/play-events! events)))
  nil)

(defn- reset-to-initial-state!
  [play-events?]
  (let [prewarmed? (state-audio-prewarmed? @state*)
        initial-state (-> (core/init-state)
                          (scene/with-expanded-collision-rules)
                          (assoc :audio-prewarmed? prewarmed?))]
    (reset! held-buttons* idle-held-buttons)
    (reset! overlay-animation* idle-overlay-animation)
    (deactivate-segment-watch!)
    (install-initial-state! initial-state play-events?)))

(defn reset-runtime!
  []
  (reset-to-initial-state! true)
  nil)

(defn bootstrap-runtime!
  "Config-time init (title state + early audio warmup). Remaining startup work
  runs in start-runtime! after config load/autorelease-pool drain."
  []
  (reset-to-initial-state! false)
  (ensure-audio-prewarmed!)
  nil)

(defn start-runtime!
  [& _args]
  (ensure-audio-prewarmed!)
  (sound-demos/play-startup-entertainer!)
  (configure-input-watchers!)
  (restart-overlay-animation!)
  nil)

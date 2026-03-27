(ns tiny-breakout.runtime
  (:require [tiny-breakout.core :as core]
            [tiny-breakout.audio :as audio]
            [tiny-breakout.scene :as scene]
            [tiny-clj.event :as event]
            [tiny-clj.runtime :as runtime]
            [tiny-fx.gfx-collision :as gfx-collision]))

(def state* (atom nil))
(def scene* (atom nil))
(def ^:private idle-overlay-animation {:text "" :start-ms 0})
(def ^:private overlay-animation* (atom idle-overlay-animation))
(def idle-held-buttons {:left false :right false :last nil})
(def held-buttons* (atom idle-held-buttons))

(def ^:private segment-watch-id :tiny-breakout/segment-end)
(def ^:private segment-watch-base-opts {:slot :game
                                        :entity-id :ball})
(def ^:private segment-watch-active* (atom false))
(def ^:private segment-watch-segment-id* (atom nil))
(def ^:private segment-watch-field* (atom nil))
(def ^:private segment-fallback-timer-id :tiny-breakout/segment-end-fallback)
;; Coalesced timeline-kick timer: avoid schedule-0 burst buildup under heavy publish-state! churn.
;; Use a named non-zero-delay timer so repeated schedules upsert/cancel instead of queueing unbounded tasks.
(def ^:private timeline-kick-timer-id :tiny-breakout/timeline-kick)

(defn- kick-timeline-watchers-task
  []
  (event/kick-timeline-watchers!)
  nil)

(def ^:private timeline-kick-timer-spec {:id timeline-kick-timer-id
                                         :fn kick-timeline-watchers-task})



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

(defn- scene-record
  [state]
  (let [overlay (scene/overlay-text (:phase state))
        animation @overlay-animation*
        scene-state (if (and (not= overlay "")
                             (= overlay (:text animation)))
                      (assoc state :overlay-start-ms (:start-ms animation))
                      (dissoc state :overlay-start-ms))]
    (record-from-map 'FrameScene (dissoc (scene/build-scene scene-state) :type))))

(defn- overlay-animation-for-state
  [state]
  (let [overlay (scene/overlay-text (:phase state))]
    (if (= overlay "")
      idle-overlay-animation
      {:text overlay
       :start-ms (current-time-ms)})))

(defn- sync-overlay-animation!
  [previous-state next-state]
  (let [previous-overlay (scene/overlay-text (:phase previous-state))
        next-overlay (scene/overlay-text (:phase next-state))]
    (cond
      (= next-overlay "")
      (reset! overlay-animation* idle-overlay-animation)

      (not= previous-overlay next-overlay)
      (reset! overlay-animation* {:text next-overlay
                                  :start-ms (current-time-ms)})

      :else nil)))

(defn- restart-overlay-animation!
  []
  (let [state @state*]
    (reset! overlay-animation* (overlay-animation-for-state state))
    (when (map? state)
      (reset! scene* (scene-record state))))
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

(defn- apply-held-paddle-direction!
  []
  (let [now-ms (current-time-ms)
        current (sync-paddle-state @tiny-breakout.runtime/state* now-ms)
        desired-dir (desired-paddle-direction @tiny-breakout.runtime/held-buttons*)
        current-dir (paddle-motion-direction current)]
    (cond
      (nil? desired-dir)
      (tiny-breakout.runtime/publish-state! (stop-paddle-motion current))

      (= desired-dir current-dir)
      nil

      :else
      (tiny-breakout.runtime/publish-state! (plan-paddle-motion (stop-paddle-motion current)
                                                                desired-dir
                                                                now-ms)))))

(defn- update-held-button!
  [button-id pressed?]
  (swap! tiny-breakout.runtime/held-buttons*
         (fn [held]
           (assoc held
                  button-id pressed?
                  :last (if pressed? button-id (get held :last)))))
  (apply-held-paddle-direction!))

(defn- state-after-input
  [input-map]
  (let [now-ms (current-time-ms)
        current (sync-paddle-state @tiny-breakout.runtime/state* now-ms)
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
                  {:id tiny-breakout.runtime/segment-fallback-timer-id
                   :fn tiny-breakout.runtime/on-segment-fallback-timer!}))
      (cancel-timer tiny-breakout.runtime/segment-fallback-timer-id)))
  nil)

(defn- segment-watch-field
  [segment]
  (let [from-x (:from-x segment)
        to-x (:to-x segment)
        from-y (:from-y segment)
        to-y (:to-y segment)
        vertical? (and (number? from-x)
                       (number? to-x)
                       (= from-x to-x)
                       (number? from-y)
                       (number? to-y)
                       (not= from-y to-y))]
    (if vertical?
      :y
      :x)))

(defn- segment-watch-opts-for-state
  [state]
  (assoc segment-watch-base-opts
         :field (segment-watch-field (:ball-segment state))))

(defn- register-segment-watch!
  [state]
  (let [opts (segment-watch-opts-for-state state)]
    (event/on {:source :timeline :id segment-watch-id}
              on-segment-timeline-event!
              opts)
    (reset! segment-watch-active* true)
    (reset! segment-watch-field* (:field opts))))

(defn- activate-segment-watch!
  []
  (when (and (map? (:ball-segment @state*))
             (not @segment-watch-active*))
    (register-segment-watch! @state*))
  nil)

(defn- store-published-state!
  [state]
  (let [state (scene/with-expanded-collision-rules state)
        events (:events state)
        state-without-events (assoc state :events [])]
    (reset! state* state-without-events)
    (reset! scene* (scene-record state-without-events))
    {:events events
     :state state-without-events}))

(defn- kick-segment-watchers!
  [state]
  (when (map? (:ball-segment state))
    (event/kick-timeline-watchers!))
  nil)

(defn- schedule-audio-events!
  [events]
  (when (seq events)
    (schedule 0 (fn breakout-audio-events-task []
                  (audio/play-events! events)
                  nil)))
  nil)

(defn- publish-state-core!
  "Updates state atom, plays audio, manages segment watchers. Returns
  state-without-events for optional immediate scene rebuild by callers."
  [state]
  (let [previous-state @state*
        state (scene/with-expanded-collision-rules state)
        events (:events state)
        state-without-events (assoc state :events [])
        segment (:ball-segment state-without-events)
        segment-id (if (map? segment) (:id segment) nil)
        segment-field (if (map? segment)
                        (segment-watch-field segment)
                        nil)
        end-ms (if (map? segment) (:end-ms segment) nil)]
    (sync-overlay-animation! previous-state state-without-events)
    (when (and (map? (:ball-segment state-without-events))
               @event/gfx-timeline-loaded?)
      (when (or (not @tiny-breakout.runtime/segment-watch-active*)
                (not= segment-field @tiny-breakout.runtime/segment-watch-field*))
        (register-segment-watch! state-without-events)))
    (if (and @tiny-breakout.runtime/segment-watch-active*
             (number? segment-id))
      (do
        (when (not= segment-id @tiny-breakout.runtime/segment-watch-segment-id*)
          (event/rearm-timeline-watch-edge! tiny-breakout.runtime/segment-watch-id))
        (reset! tiny-breakout.runtime/segment-watch-segment-id* segment-id))
      (do
        (reset! tiny-breakout.runtime/segment-watch-segment-id* nil)
        (reset! tiny-breakout.runtime/segment-watch-field* nil)))
    (if (and (number? segment-id)
             (number? end-ms))
      (let [delay-ms (max 1 (- end-ms (current-time-ms)))]
        (schedule delay-ms {:id tiny-breakout.runtime/segment-fallback-timer-id
                            :fn tiny-breakout.runtime/on-segment-fallback-timer!}))
      (cancel-timer tiny-breakout.runtime/segment-fallback-timer-id))
    (reset! state* state-without-events)
    (if (and (map? (:ball-segment state-without-events))
             @event/gfx-timeline-loaded?)
      (schedule 1 tiny-breakout.runtime/timeline-kick-timer-spec)
      nil)
    (schedule-audio-events! events)
    state-without-events))

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
    (tiny-breakout.runtime/publish-state! next-state)))

(defn on-spatial-event!
  "Handles one spatial collision event from the render thread.
  Paddle-only since brick collisions are resolved predictively by fx/sweep-aabb."
  [event]
  (let [now-ms (current-time-ms)
        next-state (core/apply-spatial-event @tiny-breakout.runtime/state* event now-ms)]
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

(defn reset-runtime!
  []
  (reset! tiny-breakout.runtime/held-buttons* tiny-breakout.runtime/idle-held-buttons)
  (reset! tiny-breakout.runtime/overlay-animation* tiny-breakout.runtime/idle-overlay-animation)
  (cancel-timer tiny-breakout.runtime/segment-fallback-timer-id)
  (cancel-timer tiny-breakout.runtime/timeline-kick-timer-id)
  (reset! tiny-breakout.runtime/segment-watch-segment-id* nil)
  (reset! tiny-breakout.runtime/segment-watch-field* nil)
  (when @tiny-breakout.runtime/segment-watch-active*
    (event/on {:source :timeline :id tiny-breakout.runtime/segment-watch-id} nil)
    (reset! tiny-breakout.runtime/segment-watch-active* false))
  (let [state (scene/with-expanded-collision-rules (core/init-state))
        events (:events state)
        state-without-events (assoc state :events [])]
    (reset! tiny-breakout.runtime/overlay-animation*
            (tiny-breakout.runtime/overlay-animation-for-state state-without-events))
    (reset! tiny-breakout.runtime/state* state-without-events)
    (reset! tiny-breakout.runtime/scene* (tiny-breakout.runtime/scene-record state-without-events))
    (when (seq events)
      (audio/play-events! events)))
  nil)

(defn bootstrap-runtime!
  "Lightweight config-time init (title state). Actual skip-to-play happens in
  start-runtime! (startup callback) so the heavy work runs after config load
  and autorelease pool drain — avoids OOM during viewer_load_game_demo_config."
  []
  (reset! tiny-breakout.runtime/held-buttons* tiny-breakout.runtime/idle-held-buttons)
  (reset! tiny-breakout.runtime/overlay-animation* tiny-breakout.runtime/idle-overlay-animation)
  (cancel-timer tiny-breakout.runtime/segment-fallback-timer-id)
  (cancel-timer tiny-breakout.runtime/timeline-kick-timer-id)
  (reset! tiny-breakout.runtime/segment-watch-segment-id* nil)
  (reset! tiny-breakout.runtime/segment-watch-field* nil)
  (when @tiny-breakout.runtime/segment-watch-active*
    (event/on {:source :timeline :id tiny-breakout.runtime/segment-watch-id} nil)
    (reset! tiny-breakout.runtime/segment-watch-active* false))
  (let [state (scene/with-expanded-collision-rules (core/init-state))
        state-without-events (assoc state :events [])]
    (reset! tiny-breakout.runtime/overlay-animation*
            (tiny-breakout.runtime/overlay-animation-for-state state-without-events))
    (reset! tiny-breakout.runtime/state* state-without-events)
    (reset! tiny-breakout.runtime/scene* (tiny-breakout.runtime/scene-record state-without-events)))
  nil)

(defn start-runtime!
  [& _args]
  (event/preload-timeline-runtime!)
  (tiny-breakout.runtime/configure-input-watchers!)
  (tiny-breakout.runtime/restart-overlay-animation!)
  nil)

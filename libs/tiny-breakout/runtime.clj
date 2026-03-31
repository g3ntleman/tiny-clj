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
(def ^:private segment-end-timer-id* (atom nil))

(defn- state-audio-prewarmed?
  "Returns true when runtime state already marks audio prewarmed."
  [state]
  (= true (:audio-prewarmed? state)))

(defn- ensure-audio-prewarmed!
  "Prewarms audio once and persists the prewarm marker in runtime state."
  []
  (when-not (state-audio-prewarmed? @state*)
    (audio/prewarm-engine!)
    (when (map? @state*)
      (swap! state* assoc :audio-prewarmed? true)))
  nil)

(defn- button-down-event?
  "Returns true when an event represents a semantic button-down."
  [event]
  (let [kind (get event :kind)]
    (or (= :button/down kind)
        (and (= 0 (get event :value))
             (nil? (get event :pressed-ms))
             (nil? (get event :held-ms))))))

(defn- button-release-event?
  "Returns true when an event represents a semantic button-release."
  [event]
  (let [kind (get event :kind)]
    (or (= :button/up kind)
        (and (not= :button/click kind)
             (= 1 (get event :value))
             (number? (get event :pressed-ms))
             (nil? (get event :held-ms))))))

(defn- overlay-animation-for-state
  "Builds initial overlay animation data for the current phase."
  [state now-ms]
  (let [overlay (scene/overlay-text (:phase state))]
    (if (= overlay "")
      idle-overlay-animation
      {:text overlay
       :start-ms now-ms})))

(defn- next-overlay-animation
  "Computes the next overlay animation from phase transitions."
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
  "Injects overlay animation timestamps into scene state."
  [state animation]
  (let [overlay (scene/overlay-text (:phase state))]
    (if (and (not= overlay "")
             (= overlay (:text animation)))
      (assoc state :overlay-start-ms (:start-ms animation))
      (dissoc state :overlay-start-ms))))

(defn- scene-record-for-animation
  "Builds a FrameScene record using a provided overlay animation."
  [state animation]
  (record-from-map 'FrameScene
                   (dissoc (scene/build-scene (scene-state-with-overlay-animation state animation))
                           :type)))

(defn- scene-record
  "Builds a FrameScene record using the current overlay animation atom."
  [state]
  (scene-record-for-animation state @overlay-animation*))

(defn- normalized-state-without-effects
  "Returns state with transient effect transport keys removed."
  [state]
  (if (contains? state :effects)
    (dissoc state :effects)
    state))

(defn- restart-overlay-animation!
  "Restarts overlay animation from current state and rebuilds the scene."
  []
  (let [state @state*
        now-ms (current-time-ms)
        animation (overlay-animation-for-state state now-ms)]
    (reset! overlay-animation* animation)
    (when (map? state)
      (reset! scene* (scene-record-for-animation state animation))))
  nil)

(defn- on-segment-timeline-event!
  "Handles timeline end notifications and applies segment-end transitions."
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
        ready? (if (number? end-ms)
                 (<= end-ms now-ms)
                 at-end?)
        resume-ms (if (number? end-ms)
                    (max now-ms end-ms)
                    now-ms)]
    (when (and (map? segment)
               (number? segment-id)
               ready?)
      (let [latest @state*
            latest-segment (:ball-segment latest)]
        (when (and (map? latest-segment)
                   (= segment-id (:id latest-segment)))
          (publish-transition!
           (core/step latest
                      {:type :game/segment-ended
                       :segment-id segment-id}
                      resume-ms))))))
  nil)

(defn- cancel-segment-end-timer!
  "Cancels pending fallback segment-end timer and clears timer state."
  []
  (let [timer-id @segment-end-timer-id*]
    (when (number? timer-id)
      (cancel-timer timer-id)))
  (reset! segment-end-timer-id* nil)
  nil)

(defn- schedule-segment-end-timer!
  "Schedules fallback segment-end timer for the active segment deadline."
  [segment-id end-ms]
  (cancel-segment-end-timer!)
  (when (and (number? segment-id)
             (number? end-ms))
    (let [delay-ms (max 0 (- end-ms (current-time-ms)))
          timer-id
          (schedule delay-ms
                    (fn []
                      (let [latest @state*
                            latest-segment (:ball-segment latest)
                            now-ms (current-time-ms)]
                        (when (and (map? latest-segment)
                                   (= segment-id (:id latest-segment)))
                          (publish-transition!
                           (core/step latest
                                      {:type :game/segment-ended
                                       :segment-id segment-id}
                                      now-ms))))
                      nil))]
      (reset! segment-end-timer-id* timer-id)))
  nil)

(defn- rendered-entity-state
  "Returns renderer snapshot state for one entity in the game slot."
  [entity-id]
  (runtime/renderer-state :game entity-id))

(defn- rendered-ball-position
  "Returns rendered ball position map when renderer state is available."
  []
  (let [rendered (rendered-entity-state :ball)]
    (if (and (map? rendered)
             (number? (:tx rendered))
             (number? (:ty rendered)))
      {:x (:tx rendered)
       :y (:ty rendered)}
      nil)))

(defn- clamp-paddle-x
  "Clamps paddle x position to playfield bounds."
  [x]
  (let [right-limit (- core/playfield-width core/paddle-width)]
    (max 0 (min right-limit x))))

(defn- attached-ball-x
  "Returns served ball x aligned to paddle center."
  [paddle-x]
  (+ paddle-x (quot core/paddle-width 2)))

(defn- attached-ball-y
  "Returns served ball y offset above the paddle."
  []
  (- core/paddle-y 6))

(defn- stopped-paddle-motion
  "Returns state with paddle motion removed."
  [state]
  (assoc state :paddle-motion nil))

(defn- paddle-motion-x
  "Computes interpolated paddle x for the given timestamp."
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
  "Synchronizes paddle and served ball positions for current time."
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
      (stopped-paddle-motion state3)
      state3)))

(defn- paddle-motion-duration-ms
  "Computes paddle travel duration in milliseconds for distance."
  [distance]
  (if (<= distance 0)
    0
    (quot (+ (* distance core/segment-step-ms)
             (- core/paddle-speed 1))
          core/paddle-speed)))

(defn- paddle-motion-direction
  "Returns current paddle motion direction or nil."
  [state]
  (let [motion (:paddle-motion state)
        dir (:dir motion)]
    (if (number? dir) dir nil)))

(defn- plan-paddle-motion
  "Plans paddle motion toward the requested edge direction."
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
  "Resolves effective paddle direction from held button state."
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
  "Updates held-button map for one button state transition."
  [held button-id pressed?]
  (assoc held
         button-id pressed?
         :last (if pressed? button-id (get held :last))))

(defn- next-state-for-held-paddle-direction
  "Builds next state for updated held-button direction."
  [state held now-ms]
  (let [current (sync-paddle-state state now-ms)
        desired-dir (desired-paddle-direction held)
        current-dir (paddle-motion-direction current)]
    (cond
      (nil? desired-dir)
      (if current-dir
        (stopped-paddle-motion current)
        nil)

      (= desired-dir current-dir)
      nil

      :else
      (plan-paddle-motion (stopped-paddle-motion current)
                          desired-dir
                          now-ms))))

(defn- apply-held-paddle-direction!
  "Applies held-button direction updates to runtime state."
  [held]
  (let [now-ms (current-time-ms)
        current-state @state*
        next-state (next-state-for-held-paddle-direction current-state held now-ms)]
    (when (and (map? next-state)
               (not (identical? current-state next-state)))
      (publish-state! next-state))))

(defn- update-held-button!
  "Updates held-button atom and applies resulting paddle direction."
  [button-id pressed?]
  (let [held @held-buttons*
        was-pressed? (= true (get held button-id))
        next-pressed? (= true pressed?)]
    (when (not= was-pressed? next-pressed?)
      (let [next-held (swap! held-buttons* next-held-buttons button-id next-pressed?)]
        (apply-held-paddle-direction! next-held)))))

(defn- normalize-paddle-intent
  "Normalizes raw input map into bounded dx/launch/pause intent."
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
  "Handles left/right movement button events."
  [button-id event]
  (cond
    (button-down-event? event)
    (update-held-button! button-id true)

    (button-release-event? event)
    (update-held-button! button-id false)

    :else nil))

(defn- on-action-button-event!
  "Handles action button down events and maps them to input."
  [input-map event]
  (when (button-down-event? event)
    (apply-input! input-map))
  nil)

(defn- on-launch-button-event!
  "Handles launch button down events."
  [event]
  (when (button-down-event? event)
    (apply-input! {:launch true}))
  nil)

(defn- on-left-button-event!
  "Handles :left button events via the shared movement handler."
  [event]
  (on-movement-button-event! :left event))

(defn- on-right-button-event!
  "Handles :right button events via the shared movement handler."
  [event]
  (on-movement-button-event! :right event))

(defn- on-pause-button-event!
  "Handles :y button events and maps them to pause input."
  [event]
  (on-action-button-event! {:pause true} event))

(defn- publish-transition!
  "Publishes one core transition map {:state ... :effects ...} and rebuilds scene
  only when gameplay state identity changed."
  [transition]
  (let [next-state (:state transition)
        effects (:effects transition)]
    (when (map? next-state)
      (let [now-ms (current-time-ms)
            previous-state @state*
            segment-watch-active? @segment-watch-active*
            segment-watch-segment-id @segment-watch-segment-id*
            state (scene/with-expanded-collision-rules next-state)
            state-without-effects (normalized-state-without-effects state)
            state-changed? (not (identical? previous-state state-without-effects))]
        (if state-changed?
          (let [current-overlay-animation @overlay-animation*
                segment (:ball-segment state-without-effects)
                segment-id (when (map? segment) (:id segment))
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
                overlay-animation (next-overlay-animation current-overlay-animation
                                                         previous-state
                                                         state-without-effects
                                                         now-ms)]
            (reset! overlay-animation* overlay-animation)
            (when install-segment-watch?
              (event/on {:source :timeline :id segment-watch-id}
                        on-segment-timeline-event!
                        segment-progress-source)
              (reset! segment-watch-active* true))
            (when rearm-segment-watch?
              (event/rearm-timeline-watch-edge! segment-watch-id))
            (reset! segment-watch-segment-id* next-segment-watch-id)
            (let [segment-end-ms (when (map? segment) (:end-ms segment))
                  timer-id @segment-end-timer-id*]
              (cond
                (and (number? next-segment-watch-id)
                     (number? segment-end-ms)
                     (or rearm-segment-watch?
                         (not (number? timer-id))))
                (schedule-segment-end-timer! next-segment-watch-id segment-end-ms)

                (not (and (number? next-segment-watch-id)
                          (number? segment-end-ms)))
                (cancel-segment-end-timer!)

                :else nil))
            (reset! state* state-without-effects)
            (reset! scene* (scene-record state-without-effects)))
          nil)
        (when (seq effects)
          (audio/play-events! effects)))))
  nil)

(defn publish-state!
  "Publishes one explicit runtime state map and optional effect cues in :effects."
  [state]
  (when (map? state)
    (publish-transition! {:state (normalized-state-without-effects state)
                          :effects (:effects state)}))
  nil)

(defn apply-input!
  "Applies one raw input map to runtime state and scene."
  [input-map]
  (let [current-state @state*
        now-ms (current-time-ms)
        intent (normalize-paddle-intent input-map)
        current (sync-paddle-state current-state now-ms)
        current (if (not= 0 (get intent :dx))
                  (stopped-paddle-motion current)
                  current)
        rendered (if (and (= (:phase current) :play)
                          (= true (:pause? intent)))
                   (rendered-ball-position)
                   nil)
        transition (core/step current
                              {:type :game/input
                               :input intent
                               :rendered-ball rendered}
                              now-ms)
        next-state (:state transition)]
    (when (and (map? next-state)
               (not (or (identical? current-state next-state)
                        (= current-state next-state))))
      (publish-transition! transition))))

(defn- collision-rule-id
  "Extracts a stable collision rule id from one host collision payload."
  [event]
  (let [id (:id event)
        rule (:rule event)
        rule-id (if (map? rule) (:id rule) nil)]
    (if (nil? id) rule-id id)))

(defn- collision-event->game-event
  "Maps host collision payloads to breakout domain events."
  [event]
  (let [rule-id (collision-rule-id event)
        phase (:phase event)
        self-aabb (:self-aabb event)
        other-aabb (:other-aabb event)
        ball-x (:min-x self-aabb)
        ball-y (:min-y self-aabb)
        paddle-x (:min-x other-aabb)
        paddle-y (:min-y other-aabb)]
    (when (and (= rule-id :ball-vs-paddle)
               (= phase :enter)
               (number? ball-x)
               (number? ball-y)
               (number? paddle-x))
      {:type :game/ball-hit-paddle
       :ball-x ball-x
       :ball-y ball-y
       :paddle-x paddle-x
       :paddle-y paddle-y})))

(defn on-game-collision-event!
  "Handles one renderer collision callback and maps it to breakout gameplay events."
  [event]
  (let [game-event (collision-event->game-event event)]
    (when (map? game-event)
      (publish-transition! (core/step @state* game-event (current-time-ms))))
    (gfx-collision/dispatch-spatial-watchers! event))
  nil)

(defn configure-input-watchers!
  "Registers breakout button watchers on the generic event bus."
  []
  (event/on {:source :button :id :left} on-left-button-event!)
  (event/on {:source :button :id :right} on-right-button-event!)
  (event/on {:source :button :id :fire} on-launch-button-event!)
  (event/on {:source :button :id :y} on-pause-button-event!)
  nil)

(defn- deactivate-segment-watch!
  "Removes segment timeline watcher and clears fallback timer."
  []
  (cancel-segment-end-timer!)
  (reset! segment-watch-segment-id* nil)
  (when @segment-watch-active*
    (event/on {:source :timeline :id segment-watch-id} nil)
    (reset! segment-watch-active* false))
  nil)

(defn- install-initial-state!
  "Installs initial runtime/scene atoms and optionally plays events."
  [state play-events?]
  (let [now-ms (current-time-ms)
        events (:effects state)
        state-without-events (normalized-state-without-effects state)
        animation (overlay-animation-for-state state-without-events now-ms)]
    (reset! overlay-animation* animation)
    (reset! state* state-without-events)
    (reset! scene* (scene-record-for-animation state-without-events animation))
    (when (and play-events? (seq events))
      (audio/play-events! events)))
  nil)

(defn- reset-to-initial-state!
  "Resets runtime atoms to deterministic initial breakout state."
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
  "Resets breakout runtime to initial state and plays reset events."
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
  "Starts breakout runtime watchers, startup audio, and overlay animation."
  [& _args]
  (ensure-audio-prewarmed!)
  (sound-demos/play-startup-entertainer!)
  (configure-input-watchers!)
  (restart-overlay-animation!)
  nil)

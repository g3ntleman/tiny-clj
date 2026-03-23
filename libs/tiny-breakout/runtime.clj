(ns tiny-breakout.runtime
  (:require [tiny-breakout.core :as core]
            [tiny-breakout.audio :as audio]
            [tiny-breakout.scene :as scene]
            [tiny-clj.event :as event]))

(def state* (atom nil))
(def scene* (atom nil))
(def idle-held-buttons {:left false :right false :last nil})
(def held-buttons* (atom idle-held-buttons))

(def ^:private segment-watch-id :tiny-breakout/segment-end)
(def ^:private segment-watch-opts {:slot :game :entity-id 1003 :field :x})
(def ^:private segment-watch-active* (atom false))
(def ^:private segment-watch-segment-id* (atom nil))
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
(def ^:private renderer-state-symbol 'tiny-clj.runtime/renderer-state)
(def ^:private runtime-play-namespace 'tiny-breakout.runtime-play)

(defn- runtime-play-call!
  [name & args]
  (require runtime-play-namespace)
  (apply (eval (symbol "tiny-breakout.runtime-play" name)) args))

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
  (record-from-map 'FrameScene (dissoc (tiny-breakout.scene/build-scene state) :type)))

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
  ((eval renderer-state-symbol) :game entity-id))

(defn- rendered-ball-position
  []
  (let [rendered (rendered-entity-state 1003)]
    (if (and (map? rendered)
             (number? (:tx rendered))
             (number? (:ty rendered)))
      {:x (:tx rendered)
       :y (:ty rendered)}
      nil)))

(defn- clamp-paddle-x
  [x]
  (let [right-limit (- tiny-breakout.core/playfield-width tiny-breakout.core/paddle-width)]
    (max 0 (min right-limit x))))

(defn- attached-ball-x
  [paddle-x]
  (+ paddle-x (quot tiny-breakout.core/paddle-width 2)))

(defn- attached-ball-y
  []
  (- tiny-breakout.core/paddle-y 6))

(defn- play-events!
  [events]
  (when (seq events)
    (audio/play-events! events))
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

(defn- activate-segment-watch!
  []
  (when (and (map? (:ball-segment @state*))
             (not @segment-watch-active*))
    (event/on {:source :timeline :id segment-watch-id}
              on-segment-timeline-event!
              segment-watch-opts)
    (reset! segment-watch-active* true))
  nil)

(defn- store-published-state!
  [state]
  (let [state (tiny-breakout.scene/with-expanded-collision-rules state)
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

(defn publish-state!
  [state]
  (let [state (scene/with-expanded-collision-rules state)
        events (:events state)
        state-without-events (assoc state :events [])
        segment (:ball-segment state-without-events)
        segment-id (if (map? segment) (:id segment) nil)
        end-ms (if (map? segment) (:end-ms segment) nil)]
    (when (and (map? (:ball-segment state-without-events))
               (not @tiny-breakout.runtime/segment-watch-active*)
               @event/gfx-timeline-loaded?)
      (event/on {:source :timeline :id tiny-breakout.runtime/segment-watch-id}
                tiny-breakout.runtime/on-segment-timeline-event!
                tiny-breakout.runtime/segment-watch-opts)
      (reset! tiny-breakout.runtime/segment-watch-active* true))
    (if (and @tiny-breakout.runtime/segment-watch-active*
             (number? segment-id))
      (do
        (when (not= segment-id @tiny-breakout.runtime/segment-watch-segment-id*)
          (event/rearm-timeline-watch-edge! tiny-breakout.runtime/segment-watch-id))
        (reset! tiny-breakout.runtime/segment-watch-segment-id* segment-id))
      (reset! tiny-breakout.runtime/segment-watch-segment-id* nil))
    (if (and (number? segment-id)
             (number? end-ms))
      (let [delay-ms (max 1 (- end-ms (current-time-ms)))]
        (schedule delay-ms {:id tiny-breakout.runtime/segment-fallback-timer-id
                            :fn tiny-breakout.runtime/on-segment-fallback-timer!}))
      (cancel-timer tiny-breakout.runtime/segment-fallback-timer-id))
    (reset! state* state-without-events)
    (reset! scene* (scene-record state-without-events))
    (if (and (map? (:ball-segment state-without-events))
             @event/gfx-timeline-loaded?)
      (do
        ;; Defer kick: synchronous kick from inside a timeline poll/callback can
        ;; re-enter poll-watchers! on the same C stack and overflow (runloop test).
        ;; Keep this coalesced via named timer to prevent task-queue growth under collision bursts.
        (schedule 1 tiny-breakout.runtime/timeline-kick-timer-spec))
      nil)
    (play-events! events))
  nil)

(defn apply-input!
  [input-map]
  (runtime-play-call! "apply-input!" input-map))

(defn on-spatial-event!
  [event]
  (runtime-play-call! "on-spatial-event!" event))

(defn configure-input-watchers!
  []
  (runtime-play-call! "configure-input-watchers!"))

(defn reset-runtime!
  []
  (reset! tiny-breakout.runtime/held-buttons* tiny-breakout.runtime/idle-held-buttons)
  (cancel-timer tiny-breakout.runtime/segment-fallback-timer-id)
  (cancel-timer tiny-breakout.runtime/timeline-kick-timer-id)
  (reset! tiny-breakout.runtime/segment-watch-segment-id* nil)
  (when @tiny-breakout.runtime/segment-watch-active*
    (event/on {:source :timeline :id tiny-breakout.runtime/segment-watch-id} nil)
    (reset! tiny-breakout.runtime/segment-watch-active* false))
  (let [state (scene/with-expanded-collision-rules (core/init-state))
        events (:events state)
        state-without-events (assoc state :events [])]
    (reset! tiny-breakout.runtime/state* state-without-events)
    (reset! tiny-breakout.runtime/scene* (tiny-breakout.runtime/scene-record state-without-events))
    (play-events! events))
  nil)

(defn bootstrap-runtime!
  "Lightweight config-time init (title state). Actual skip-to-play happens in
  start-runtime! (startup callback) so the heavy work runs after config load
  and autorelease pool drain — avoids OOM during viewer_load_game_demo_config."
  []
  (try
    (audio/preload-tracks!)
    (catch RuntimeException _
      nil)
    (catch Exception _
      nil))
  (reset! tiny-breakout.runtime/held-buttons* tiny-breakout.runtime/idle-held-buttons)
  (cancel-timer tiny-breakout.runtime/segment-fallback-timer-id)
  (cancel-timer tiny-breakout.runtime/timeline-kick-timer-id)
  (reset! tiny-breakout.runtime/segment-watch-segment-id* nil)
  (when @tiny-breakout.runtime/segment-watch-active*
    (event/on {:source :timeline :id tiny-breakout.runtime/segment-watch-id} nil)
    (reset! tiny-breakout.runtime/segment-watch-active* false))
  (let [state (tiny-breakout.scene/with-expanded-collision-rules (tiny-breakout.core/init-state))
        state-without-events (assoc state :events [])]
    (reset! tiny-breakout.runtime/state* state-without-events)
    (reset! tiny-breakout.runtime/scene* (tiny-breakout.runtime/scene-record state-without-events)))
  nil)

(defn start-runtime!
  [& _args]
  (event/preload-timeline-runtime!)
  (tiny-breakout.runtime/configure-input-watchers!)
  nil)

;; Preload the split runtime helper during namespace load so heap probes do not
;; attribute its one-time source/var setup to the first gameplay cycle.
(require 'tiny-breakout.runtime-play)

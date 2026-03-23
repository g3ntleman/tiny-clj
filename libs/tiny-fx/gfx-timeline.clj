(ns tiny-fx.gfx-timeline
  (:require [tiny-clj.runtime :as runtime]
            [tiny-clj.event :as event]))

(def timeline-watchers* (atom {}))

(def ^:private poll-timer-id :tiny-fx/timeline-watch-poll)
(def ^:private poll-period-ms 1)
;; When next-poll-delay-ms is nil (e.g. renderer still shows stale :at-end after a fired
;; callback, or no snapshot yet), poll at ~60fps instead of 1ms to avoid starving the loop.
(def ^:private coarse-poll-ms 16)

(defn- validate-watch
  [id f opts]
  (when (nil? id)
    (throw "timeline/watch requires id"))
  (when (not (or (nil? f) (fn? f)))
    (throw "timeline/watch expects fn or nil"))
  (when (and f (not (keyword? (:slot opts))))
    (throw "timeline/watch requires keyword :slot"))
  (when (and f (nil? (:entity-id opts)))
    (throw "timeline/watch requires :entity-id"))
  (when (and f (not (keyword? (:field opts))))
    (throw "timeline/watch requires keyword :field")))

(defn- next-poll-delay-ms
  []
  (let [watcher-entries (vec @timeline-watchers*)]
    (loop [i 0
           best-delay nil]
      (if (< i (count watcher-entries))
        (let [[_ watcher] (nth watcher-entries i)
              progress (runtime/renderer-timeline-progress (:slot watcher)
                                                          (:entity-id watcher)
                                                          (:field watcher))
              flagged (= true (:end-event progress))
              at-end (and flagged (= true (:at-end progress)))
              was-at-end (= true (:last-at-end watcher))
              phase-ms (let [v (:phase-ms progress)] (if (number? v) v 0))
              period-ms (let [v (:period-ms progress)] (if (number? v) v 0))
              remaining-ms (if (> period-ms phase-ms)
                             (- period-ms phase-ms)
                             poll-period-ms)
              next-delay (cond
                           (nil? progress) nil
                           (and at-end (not was-at-end)) poll-period-ms
                           (and flagged (not at-end)) (max poll-period-ms remaining-ms)
                           :else nil)
              next-best (cond
                          (nil? next-delay) best-delay
                          (nil? best-delay) next-delay
                          (< next-delay best-delay) next-delay
                          :else best-delay)]
          (recur (inc i) next-best))
        (when (number? best-delay)
          (if (> best-delay 0) best-delay poll-period-ms))))))

(defn poll-watchers!
  "Runtime helper: checks watched timelines and emits one callback on each false->true :at-end edge."
  []
  (let [watchers @timeline-watchers*]
    (let [callback-fired?
          (loop [remaining (seq watchers)
                 fired? false]
            (if (seq remaining)
              (let [[watch-id watcher] (first remaining)
                    progress (runtime/renderer-timeline-progress (:slot watcher)
                                                                (:entity-id watcher)
                                                                (:field watcher))
                    flagged (= true (:end-event progress))
                    at-end (and flagged (= true (:at-end progress)))
                    was-at-end (= true (:last-at-end watcher))]
                (when (not= was-at-end at-end)
                  (swap! timeline-watchers*
                         (fn [current]
                           (if (contains? current watch-id)
                             (assoc current
                                    watch-id
                                    (assoc (get current watch-id) :last-at-end at-end))
                             current))))
                (recur (next remaining)
                       (or fired?
                            (when (and at-end (not was-at-end))
                              ;; Defer callback to a fresh event-loop task (schedule 0). A synchronous
                              ;; call can recurse: callback -> publish-state! -> kick-timeline-watchers!
                              ;; -> poll path and blow the C eval stack (see breakout runloop test).
                              (let [cb (:callback watcher)
                                    payload {:source :timeline
                                             :id (:id watcher)
                                             :slot (:slot watcher)
                                             :entity-id (:entity-id watcher)
                                             :field (:field watcher)
                                             :progress progress}]
                                (schedule 0 (fn timeline-watch-deferred-cb [] (cb payload)))
                                true)))))
              fired?))]
      ;; A fired callback runs publish-state! -> kick-timeline-watchers! -> schedule.
      ;; Do not cancel-timer or kick-watchers! here — that used to erase the new timer.
      (when (not callback-fired?)
        (tiny-fx.gfx-timeline/kick-watchers!))))
  nil)

(defn kick-watchers!
  "Ensures timeline watchers have at most one pending wakeup.
The next poll is scheduled for the nearest known end edge, or soon when the
renderer has not published a fresh snapshot yet."
  []
  (let [watcher-entries (vec @tiny-fx.gfx-timeline/timeline-watchers*)
        delay-ms (if (zero? (count watcher-entries))
                   nil
                   (let [computed (next-poll-delay-ms)]
                     (if (number? computed) computed coarse-poll-ms)))]
    (if (number? delay-ms)
      (schedule delay-ms {:id poll-timer-id
                          :fn tiny-fx.gfx-timeline/poll-watchers!})
      (cancel-timer poll-timer-id)))
  nil)

(defn reset-watch-edge!
  "Re-arms one watcher edge by priming :last-at-end to true.
This intentionally suppresses one stale :at-end=true sample from the previous
segment; the watcher re-arms itself once it sees :at-end=false on the new
segment and then emits the next real false->true end edge."
  [watch-id]
  (when watch-id
    (swap! timeline-watchers*
           (fn [watchers]
             (if (contains? watchers watch-id)
               (assoc watchers watch-id (assoc (get watchers watch-id) :last-at-end true))
               watchers))))
  nil)

(defn watch
  "Registers or removes one timeline end watcher.

  (watch :ball-end f {:slot :game :entity-id 1003 :field :x})
  (watch :ball-end nil)

  The callback receives {:source :timeline :id ... :slot ... :entity-id ... :field ... :progress ...}."
  [& args]
  (let [argc (count args)]
    (if (or (< argc 2) (> argc 3))
      (throw (str "timeline/watch expects 2 or 3 arguments, got " argc))
      (let [id (nth args 0)
            f (nth args 1)
            opts (if (= argc 3) (nth args 2) {})]
        (tiny-fx.gfx-timeline/validate-watch id f opts)
        (reset! tiny-fx.gfx-timeline/timeline-watchers*
                (if (nil? f)
                  (dissoc @tiny-fx.gfx-timeline/timeline-watchers* id)
                  (assoc @tiny-fx.gfx-timeline/timeline-watchers*
                         id
                         {:id id
                          :slot (:slot opts)
                          :entity-id (:entity-id opts)
                          :field (:field opts)
                          :callback f
                          :last-at-end false})))
        (tiny-fx.gfx-timeline/kick-watchers!)
        nil))))

;; Mark timeline as loaded so event/on :timeline works without explicit preload.
(reset! event/gfx-timeline-loaded? true)

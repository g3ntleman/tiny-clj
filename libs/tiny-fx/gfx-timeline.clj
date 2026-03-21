(ns tiny-fx.gfx-timeline
  (:require [tiny-clj.runtime :as runtime]))

(declare poll-watchers!)

(def timeline-watchers* (atom {}))

(def ^:private poll-timer-id :tiny-fx/timeline-watch-poll)
(def ^:private poll-period-ms 1)

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

(defn poll-watchers!
  "Runtime helper: checks watched timelines and emits one callback on each false->true :at-end edge."
  []
  (let [watchers @timeline-watchers*]
    (loop [remaining (seq watchers)]
      (when (seq remaining)
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
          (when (and at-end (not was-at-end))
            ((:callback watcher)
             {:source :timeline
              :id (:id watcher)
              :slot (:slot watcher)
              :entity-id (:entity-id watcher)
              :field (:field watcher)
              :progress progress}))
          (recur (next remaining))))))
  (let [delay-ms (next-poll-delay-ms)]
    (if (number? delay-ms)
      (schedule delay-ms tiny-fx.gfx-timeline/poll-timer-spec)
      (cancel-timer poll-timer-id)))
  nil)

(def ^:private poll-timer-fn
  (fn []
    (tiny-fx.gfx-timeline/poll-watchers!)))

(def ^:private poll-timer-spec {:id poll-timer-id
                                :fn poll-timer-fn})

(defn- next-poll-delay-ms
  []
  (let [watchers @timeline-watchers*]
    (loop [remaining (seq watchers)
           best-delay nil]
      (if (seq remaining)
        (let [[_ watcher] (first remaining)
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
                             1)
              next-delay (cond
                           (nil? progress) nil
                           (and at-end (not was-at-end)) 1
                           (and flagged (not at-end)) remaining-ms
                           :else nil)
              next-best (cond
                          (nil? next-delay) best-delay
                          (nil? best-delay) next-delay
                          (< next-delay best-delay) next-delay
                          :else best-delay)]
          (recur (next remaining) next-best))
        (if (number? best-delay)
          (if (> best-delay 0) best-delay 1)
          nil)))))

(defn kick-watchers!
  "Ensures timeline watchers have at most one pending wakeup.
The next poll is scheduled for the nearest known end edge, or soon when the
renderer has not published a fresh snapshot yet."
  []
  (let [delay-ms (if (empty? @timeline-watchers*)
                   nil
                   (let [computed (next-poll-delay-ms)]
                     (if (number? computed) computed poll-period-ms)))]
    (if (number? delay-ms)
      (schedule delay-ms tiny-fx.gfx-timeline/poll-timer-spec)
      (cancel-timer poll-timer-id)))
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
        (validate-watch id f opts)
        (reset! timeline-watchers*
                (if (nil? f)
                  (dissoc @timeline-watchers* id)
                  (assoc @timeline-watchers*
                         id
                         {:id id
                          :slot (:slot opts)
                          :entity-id (:entity-id opts)
                          :field (:field opts)
                          :callback f
                          :last-at-end false})))
        (kick-watchers!)
        nil))))

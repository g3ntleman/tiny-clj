(ns tiny-clj.event
  (:require [tiny-clj.button :as button]
            [tiny-clj.sensor :as sensor]
            [tiny-fx.gfx-collision :as collision]
            [tiny-fx.gfx-timeline :as timeline]))

(defn- subscribe [descriptor callback opts]
  (let [source (get descriptor :source)
        id (get descriptor :id)]
    (when (nil? source)
      (throw "event/on requires :source"))
    (when (nil? id)
      (throw "event/on requires :id"))
    (cond
      (= source :button) (button/watch id callback opts)
      (= source :sensor) (sensor/watch id callback opts)
      (= source :spatial) (collision/watch id callback opts)
      (= source :timeline) (timeline/watch id callback opts)
      :else (throw (str "event/on: unsupported :source " source)))))

(defn on
  "Registers or removes a semantic event subscription.

  (on {:source :button :id :ok} f)
  (on :button :ok f)
  (on :button :ok f {:hold-ms 600})
  (on {:source :button :id :ok} nil)

Supported sources:
  :button  -> semantic button events
  :sensor  -> semantic sensor events
  :spatial -> semantic spatial/collision events
  :timeline -> semantic timeline-end events

Options are forwarded to the underlying source-specific runtime."
  [& args]
  (let [argc (count args)]
    (cond
      (= argc 2)
      (subscribe (nth args 0) (nth args 1) {})

      (= argc 3)
      (let [arg0 (nth args 0)]
        (if (map? arg0)
          (subscribe arg0 (nth args 1) (nth args 2))
          (subscribe {:source arg0 :id (nth args 1)} (nth args 2) {})))

      (= argc 4)
      (subscribe {:source (nth args 0) :id (nth args 1)} (nth args 2) (nth args 3))

      :else
      (throw "event/on expects 2, 3, or 4 arguments"))))

(defn kick-timeline-watchers!
  []
  (timeline/kick-watchers!)
  nil)

(defn dispatch-timeline-watch!
  "Pushes one timeline progress sample into a specific watcher.

Returns true when the watcher exists and was dispatched, else false."
  [watch-id progress & args]
  (let [opts (if (seq args) (nth args 0) {})]
    (if watch-id
      (timeline/dispatch-watch! watch-id progress opts)
      false)))

(defn dispatch-timeline-progress!
  "Pushes one timeline progress sample that contains :event-id.

Returns true when a watcher for :event-id exists and was dispatched, else false."
  [progress & args]
  (let [opts (if (seq args) (nth args 0) {})]
    (timeline/dispatch-progress! progress opts)))

(defn rearm-timeline-watch-edge!
  [watch-id]
  (when watch-id
    (timeline/reset-watch-edge! watch-id))
  nil)

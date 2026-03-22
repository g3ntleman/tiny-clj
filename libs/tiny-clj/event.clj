(ns tiny-clj.event
  (:require [tiny-clj.button :as button]
            [tiny-clj.sensor :as sensor]
            [tiny-fx.gfx-collision :as collision]))

(def gfx-timeline-loaded? (atom false))

(defn preload-timeline-runtime!
  []
  (if @gfx-timeline-loaded?
    nil
    (do
      (require 'tiny-fx.gfx-timeline)
      (reset! gfx-timeline-loaded? true)
      nil)))

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
      (= source :timeline) (if (and (nil? callback) (not @gfx-timeline-loaded?))
                             nil
                             (do
                               (when (not @gfx-timeline-loaded?)
                                 (throw "event/on: :timeline source requires preloaded tiny-fx.gfx-timeline"))
                               (tiny-fx.gfx-timeline/watch id callback opts)))
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
  (when @gfx-timeline-loaded?
    (tiny-fx.gfx-timeline/kick-watchers!))
  nil)

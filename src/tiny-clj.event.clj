R"TINY_CLJ_EVENT(
(ns tiny-clj.event
  (:require [tiny-clj.button :as button]
            [tiny-clj.sensor :as sensor]))

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
      :else (throw (str "event/on: unsupported :source " source)))))

(defn on
  "Registers or removes a semantic event subscription.

  (on {:source :button :id :ok} f)
  (on :button :ok f)
  (on :button :ok f {:hold-ms 600})
  (on {:source :button :id :ok} nil)"
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
)TINY_CLJ_EVENT"

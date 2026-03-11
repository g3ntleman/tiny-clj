R"TINY_CLJ_SENSOR(
(ns tiny-clj.sensor
  (:require [tiny-clj.board :as board]
            [tiny-clj.gpio :as gpio]))

(defn- watch-native [sensor-id pin signal threshold hysteresis stable-ms outlier-delta-max sample-period-ms range-min range-max callback] :native)

(defn watch
  "Registers or removes a logical sensor watcher.

  (watch :battery callback)
  (watch :trigger callback {:threshold 2200})
  (watch :battery nil)

Events:
  {:source :sensor, :id :battery, :kind :sensor/change, :pin 35, :value 1234, :delta 10}
  {:source :sensor, :id :trigger, :kind :sensor/threshold-crossed, :pin 36, :value 2500, :delta 42, :active true}
  {:source :sensor, :id :trigger, :kind :sensor/active, :pin 36, :value 2500, :delta 42, :active true}
  {:source :sensor, :id :trigger, :kind :sensor/inactive, :pin 36, :value 2100, :delta -50, :active false}"
  [sensor-id callback & args]
  (let [raw-opts (first args)
        opts (if (nil? raw-opts) {} raw-opts)
        base (get board/sensors sensor-id)
        cfg (if (nil? base) opts (merge base opts))
        pin (get cfg :pin)
        signal (let [v (get cfg :signal)] (if (nil? v) :analog v))
        range (let [v (get cfg :range)] (if (nil? v) [0 4095] v))]
    (when (nil? pin)
      (throw (str "sensor/watch: unknown sensor " sensor-id)))
    (when callback
      (if (= signal :analog)
        (gpio/set-pin-mode! pin :adc)
        (gpio/set-pin-mode! pin :input)))
    (watch-native sensor-id
                  pin
                  signal
                  (let [v (get cfg :threshold)] (if (nil? v) -1 v))
                  (let [v (get cfg :hysteresis)] (if (nil? v) 0 v))
                  (let [v (get cfg :stable-ms)] (if (nil? v) 0 v))
                  (let [v (get cfg :outlier-delta-max)] (if (nil? v) -1 v))
                  (let [v (get cfg :sample-period-ms)] (if (nil? v) 50 v))
                  (first range)
                  (nth range 1)
                  callback)))
)TINY_CLJ_SENSOR"

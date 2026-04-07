(ns tiny-clj.audio-event-bus
  "Bridges `tiny-fx.sound/sound-on-finished!` to `clojure.core.async` pub/sub.
  Topic key is `:source`; finished events use `:source` `:audio` → `(sub p :audio out-ch)`.
  Each `audio-finished-pub!` / `audio-finished-source!` call re-registers the global
  native callback (survives shutdown/re-init); any other `sound-on-finished!` replaces it."
  (:require [clojure.core.async :as async]
            [tiny-fx.sound :as sound]))

(defonce ^:private *bus* (atom nil))

(defn- ensure-bus!
  []
  (let [st (swap! *bus*
                  (fn [s]
                    (or s
                        (let [src (async/chan 64)]
                          {:source src :pub (async/pub src :source)}))))]
    (sound/sound-on-finished!
     (fn [event]
       (async/put! (:source st) event)
       nil))
    st))

(defn audio-finished-pub!
  "Returns the publication over all sound finished events (topic from `:source` on each event).

  Installs or refreshes the single global `sound-on-finished!` handler."
  []
  (:pub (ensure-bus!)))

(defn audio-finished-source!
  "Returns the internal source channel. For tests and advanced wiring only."
  []
  (:source (ensure-bus!)))

R"TINY_CLJ_BUTTON(
(ns tiny-clj.button
  (:require [tiny-clj.board :as board]
            [tiny-clj.gpio :as gpio]))

(defn- watch-native [control-id pin active-level debounce-ms hold-ms callback] :native)

(defn- active-level->int [active]
  (if (= active :high) 1 0))

(defn watch
  "Registers or removes a logical button watcher.

  (watch :ok callback)
  (watch :ok callback {:hold-ms 600})
  (watch :ok nil)

Events:
  {:source :button, :id :ok, :kind :button/down,  :pin 13, :value 0|1}
  {:source :button, :id :ok, :kind :button/up,    :pin 13, :pressed-ms <fixnum>}
  {:source :button, :id :ok, :kind :button/click, :pin 13, :pressed-ms <fixnum>}
  {:source :button, :id :ok, :kind :button/hold,  :pin 13, :held-ms <fixnum>}"
  [control-id callback & args]
  (let [raw-opts (first args)
        opts (if (nil? raw-opts) {} raw-opts)
        base (get board/buttons control-id)
        cfg (if (nil? base) opts (merge base opts))
        pin (get cfg :pin)]
    (when (nil? pin)
      (throw (str "button/watch: unknown control " control-id)))
    (when callback
      (gpio/set-pin-mode! pin :input (let [pull (get cfg :pull)]
                                       (if (nil? pull) {} {:pull pull}))))
    (watch-native control-id
                  pin
                  (active-level->int (let [v (get cfg :active)] (if (nil? v) :low v)))
                  (let [v (get cfg :debounce-ms)] (if (nil? v) 20 v))
                  (let [v (get cfg :hold-ms)] (if (nil? v) 450 v))
                  callback)))
)TINY_CLJ_BUTTON"

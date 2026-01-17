;; GPIO helpers for tiny-clj (macOS first, ESP32 later).
;;
;; Channel-first API implemented on top of native clojure.core/gpio-watch.

(ns tinyclj.gpio)

^#^{:doc "Creates a core.async channel backed GPIO watcher.

Args: (gpio-channel pin) or (gpio-channel pin buffer).
Returns a map {:ch ch :watcher-id wid :close! (fn [])} where events are [pin value]."}
(defn gpio-channel [& args]
  ;; Returns a map with:
  ;; - :ch         core.async channel receiving events
  ;; - :watcher-id id returned by (clojure.core/gpio-watch ...)
  ;; - :close!     function to stop watching and close the channel
  ;;
  ;; Event format: [pin value]
  (do
    (require 'clojure.core.async)
    (let [argc (count args)]
      (if (< argc 1)
        (throw "tinyclj.gpio/gpio-channel requires pin")
        (let [pin (nth args 0)
              buf (if (>= argc 2) (nth args 1) (clojure.core.async/sliding-buffer 64))
              ch (clojure.core.async/chan buf)
              wid (clojure.core/gpio-watch
                    pin
                    (fn [ev]
                      ;; Best-effort: keep latest events (sliding buffer).
                      (clojure.core.async/offer! ch ev)))]
          {:ch ch
           :watcher-id wid
           :close! (fn []
                     (do
                       (clojure.core/gpio-unwatch wid)
                       (clojure.core.async/close! ch)
                       nil))})))))


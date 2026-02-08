(ns tiny-clj.mdns
  (:require [tiny-clj.net.mdns :as m]))

;; Compatibility namespace: prefer tiny-clj.net.mdns going forward.

(def open m/open)
(def on-event m/on-event)
(def browse! m/browse!)
(def close! m/close!)


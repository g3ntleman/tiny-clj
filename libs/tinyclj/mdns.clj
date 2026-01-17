(ns tinyclj.mdns
  (:require [tinyclj.net.mdns :as m]))

;; Compatibility namespace: prefer tinyclj.net.mdns going forward.

(def open m/open)
(def on-event m/on-event)
(def browse! m/browse!)
(def close! m/close!)


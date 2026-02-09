(ns tiny-clj.net.mdns)

;; Native-backed mDNS/DNS-SD browsing + resolve API (Matter preparation).
;;
;; Notes:
;; - Browsing/resolve only (no advertise yet).
;; - Callback style, consistent with tiny-clj.net.
;; - The implementation is intended to be lazy: no sockets/state until (open) is called.

^#^{:doc "Opens the platform mDNS transport and returns an opaque handle (native)."}
(defn open [] :native)

^#^{:doc "Registers an event callback: (on-event h (fn [ev] ...)) (native)."}
(defn on-event [h f] :native)

^#^{:doc "Starts browsing a service name and sends an initial PTR query multicast (native). Example: (browse! h \"_matterc._udp.local\")."}
(defn browse! [h service] :native)

^#^{:doc "Closes the handle (native). Safe to call multiple times."}
(defn close! [h] :native)


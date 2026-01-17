(ns tinyclj.net)

;; Native-backed networking API.
;;
;; Design notes:
;; - Callback style (like http-kit) with optional channel integration (via put!/take!).
;; - Receive buffers are zero-copy: the :data byte-array references an internal packet buffer.
;;   If you need to keep data beyond the callback, copy explicitly (e.g., (aclone data)).

^#^{:doc "Creates a UDP socket bound to {:port N} (native). Returns an opaque handle."}
(defn udp-socket [opts] :native)

^#^{:doc "Registers a receive callback: (on-receive sock (fn [{:keys [data from port]}] ...)) (native)."}
(defn on-receive [sock f] :native)

^#^{:doc "Sends a UDP packet (native). Expects {:to \"ip\" :port N :data byte-array}."}
(defn send! [sock msg] :native)

^#^{:doc "Closes a UDP socket (native)."}
(defn close! [sock] :native)

^#^{:doc "Creates a TCP connection (native). Expects {:host \"...\" :port N}. Returns an opaque handle."}
(defn tcp-connect [opts] :native)

^#^{:doc "Registers a TCP receive callback: (tcp-on-receive conn (fn [{:keys [data]}] ...)) (native)."}
(defn tcp-on-receive [conn f] :native)

^#^{:doc "Sends bytes on a TCP connection (native). Expects {:data byte-array}."}
(defn tcp-send! [conn msg] :native)

^#^{:doc "Closes a TCP connection (native)."}
(defn tcp-close! [conn] :native)

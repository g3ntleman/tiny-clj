;; Minimal core.async subset for tiny-clj.
;;
;; Active target:
;; - keep a compatible public surface for the covered subset
;; - move the hot path into C builtins
;; - fail clearly for unsupported areas instead of exposing partial semantics

(ns clojure.core.async)

(defn unsupported!
  "Throws a clear unsupported marker for intentionally omitted core.async features."
  [& args]
  (let [feature (first args)
        detail (first (rest args))]
    (if (nil? detail)
      (throw (str "core.async/unsupported: " feature))
      (throw (str "core.async/unsupported: " feature " " detail)))))

(defn illegal-arg!
  "Throws a clear illegal-argument marker for invalid core.async inputs."
  [msg]
  (throw (str "core.async/illegal-arg: " msg)))

(defn buffer
  "Returns a fixed buffer descriptor for `chan`."
  [n]
  (when (or (nil? n) (not (integer? n)) (< n 0))
    (illegal-arg! (str "buffer n must be integer >= 0, got " n)))
  {:tiny-clj.buffer/type :fixed
   :tiny-clj.buffer/n n})

(defn sliding-buffer
  "Returns a sliding buffer descriptor for `chan`."
  [n]
  (when (or (nil? n) (not (integer? n)) (< n 0))
    (illegal-arg! (str "sliding-buffer n must be integer >= 0, got " n)))
  {:tiny-clj.buffer/type :sliding
   :tiny-clj.buffer/n n})

(defn dropping-buffer
  "Returns a dropping buffer descriptor for `chan`."
  [n]
  (when (or (nil? n) (not (integer? n)) (< n 0))
    (illegal-arg! (str "dropping-buffer n must be integer >= 0, got " n)))
  {:tiny-clj.buffer/type :dropping
   :tiny-clj.buffer/n n})

(defn chan
  "Creates a channel for the supported tiny-clj core.async subset."
  [& args]
  :native)

(defn put!
  "Puts a value onto a channel in the supported tiny-clj core.async subset."
  [& args]
  :native)

(defn poll!
  "Polls one value from a channel without blocking."
  [ch]
  :native)

(defn close!
  "Closes a channel in the supported tiny-clj core.async subset."
  [ch]
  :native)

(defn closed?
  "Returns true when a channel has been closed."
  [ch]
  :native)

(defn pub
  "Creates a publication view over a source channel."
  [& args]
  :native)

(defn- pub-subs-atom [p]
  (let [subs-atom (:tiny-clj.core-async/subs p)]
    (when (or (nil? subs-atom) (not (atom? subs-atom)))
      (illegal-arg! "pub is missing subscription state"))
    subs-atom))

(defn sub
  "Registers a channel subscription for one publication topic.
  Accepts the optional `close?` argument for compatibility; tiny-clj currently ignores it."
  [& args]
  (if (or (= 3 (count args))
          (= 4 (count args)))
    (let [p (nth args 0)
          topic (nth args 1)
          ch (nth args 2)
          _close? (nth args 3 true)
          subs-atom (pub-subs-atom p)]
      (swap! subs-atom
             (fn [subs]
               (let [channels (or (get subs topic) [])]
                 (if (some #(identical? % ch) channels)
                   subs
                   (assoc subs topic (conj channels ch))))))
      nil)
    (illegal-arg! (str "sub arity " (count args) " not supported"))))

(defn unsub
  "Removes one channel subscription from a publication topic."
  [p topic ch]
  (let [subs-atom (pub-subs-atom p)]
    (swap! subs-atom
           (fn [subs]
             (let [channels (get subs topic)]
               (if (nil? channels)
                 subs
                 (let [next-channels (vec (filter #(not (identical? % ch)) channels))
                       n (count channels)
                       m (count next-channels)]
                   (cond
                     (= n m) subs
                     (zero? m) (dissoc subs topic)
                     :else (assoc subs topic next-channels)))))))
    nil))

(defn unsub-all
  "Removes all subscriptions from a publication, optionally limited to one topic."
  [& args]
  (if (or (= 1 (count args))
          (= 2 (count args)))
    (let [p (nth args 0)
          topic (nth args 1 nil)
          subs-atom (pub-subs-atom p)]
      (if (= 1 (count args))
        (reset! subs-atom {})
        (swap! subs-atom
               (fn [subs]
                 (dissoc subs topic))))
      nil)
    (illegal-arg! (str "unsub-all arity " (count args) " not supported"))))

(defn offer!
  "Unsupported in the current tiny-clj core.async subset."
  [& _args]
  (unsupported! "offer!"))

(defn take!
  "Unsupported in the current tiny-clj core.async subset."
  [& _args]
  (unsupported! "take!"))

(defn timeout
  "Unsupported in the current tiny-clj core.async subset."
  [msecs]
  (let [_ msecs]
    (unsupported! "timeout")))

(defmacro go [& _body]
  (list 'clojure.core.async/unsupported! "go"))

(defmacro go-loop [bindings & body]
  (let [_ bindings
        _ body]
    (list 'clojure.core.async/unsupported! "go-loop")))

(defmacro <! [port]
  (let [_ port]
    (list 'clojure.core.async/unsupported! "<!")))

(defmacro >! [port val]
  (let [_ port
        _ val]
    (list 'clojure.core.async/unsupported! ">!")))

(defmacro alts! [ports & opts]
  (let [_ ports
        _ opts]
    (list 'clojure.core.async/unsupported! "alts!")))

;; Older experimental callback/go subset was removed; history is in git.

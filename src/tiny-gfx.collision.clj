R"TINY_GFX_COL(
(ns tiny-fx.gfx-collision)

;; Spatial callback API (C detection + Clojure response routing)
;; Keep mutable callback state in an atom (never re-def inside functions),
;; so callback assignment is stable under SlotRef lowering.
(def collision-callback* (atom nil))
(def spatial-watchers* (atom {}))

(defn- event-id
  [event]
  (let [id (get event :id)
        rule (get event :rule)
        rule-id (if rule (get rule :id) nil)]
    (if (nil? id)
      rule-id
      id)))

(defn watch
  "Registers or removes one spatial event watcher by semantic id.

  (watch :player-hit f)
  (watch :player-hit f {})
  (watch :player-hit nil)

  Returns nil. Runtime dispatch is fire-and-forget; callback return values are ignored."
  [& args]
  (let [argc (count args)]
    (if (or (< argc 2) (> argc 3))
      (throw (str "spatial/watch expects 2 or 3 arguments, got " argc))
      (let [id (nth args 0)
            f (nth args 1)]
        (when (nil? id)
          (throw "spatial/watch requires id"))
        (when (not (or (nil? f) (fn? f)))
          (throw "spatial/watch expects fn or nil"))
        (reset! spatial-watchers*
                (if (nil? f)
                  (dissoc @spatial-watchers* id)
                  (assoc @spatial-watchers* id f)))
        nil))))

^#^{:doc "Sets the global spatial callback closure. Pass nil to disable callback dispatch.
Returns the assigned callback (or nil). Runtime contract: host C dispatches this callback
through scheduler/runloop ingress and ignores callback return values."}
(def set-collision-callback!
  (fn set-collision-callback! [f]
    (if (or (nil? f) (fn? f))
      (reset! collision-callback* f)
      (throw "set-collision-callback! expects fn or nil"))))

^#^{:doc "Invokes the configured spatial callback with one event payload.
Direct Clojure invocation returns the callback value; host C dispatch path ignores return values.
Returns nil when no callback is configured."}
(def invoke-collision-callback!
  (fn invoke-collision-callback! [event]
    (let [f (deref collision-callback*)
          result (if f
                   (f event)
                   nil)
          watcher (get @spatial-watchers* (event-id event))]
      (when watcher
        (watcher event))
      result)))

)TINY_GFX_COL"

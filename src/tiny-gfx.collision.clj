R"TINY_GFX_COL(
(ns tiny-fx.gfx-collision)

;; Spatial callback API (C detection + Clojure response routing)
;; Keep mutable callback state in an atom (never re-def inside functions),
;; so callback assignment is stable under SlotRef lowering.
(def collision-callback* (atom nil))

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
    (let [f (deref collision-callback*)]
      (if f
        (f event)
        nil))))

)TINY_GFX_COL"

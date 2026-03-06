R"TINY_GFX_COL(
(ns tiny-gfx.collision)

;; collision callback API (C detection + Clojure response routing)
;; Keep mutable callback state in an atom (never re-def inside functions),
;; so callback assignment is stable under SlotRef lowering.
(def collision-callback* (atom nil))

^#^{:doc "Sets the global collision callback closure. Pass nil to disable callback dispatch.
Returns the assigned callback (or nil). Runtime contract: host C dispatches this callback
through scheduler/runloop ingress and ignores callback return values."}
(def set-collision-callback!
  (fn set-collision-callback! [f]
    (if (or (nil? f) (fn? f))
      (reset! collision-callback* f)
      (throw "set-collision-callback! expects fn or nil"))))

^#^{:doc "Invokes the configured collision callback.
Direct Clojure invocation returns the callback value; host C dispatch path ignores return values.
Returns nil when no callback is configured."}
(def invoke-collision-callback!
  (fn invoke-collision-callback! []
    (let [f (deref collision-callback*)]
      (if f
        (f)
        nil))))

^#^{:doc "Returns host-viewer demo collision policy as a vector:
[player-min-x player-max-x player-min-y-base player-max-y-base
 obstacle-min-x-base obstacle-max-x-base obstacle-min-y obstacle-max-y
 obstacle-anchor-x cooldown-ms]."}
(def player-vs-obstacle-policy
  (fn player-vs-obstacle-policy []
    [58 86 124 146
     13 27 106 146
     20 300]))

)TINY_GFX_COL"

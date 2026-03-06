R"TINY_GFX_COL(
(ns tiny-gfx.collision)

;; collision callback API (C detection + Clojure response routing)
;; Keep mutable callback state in an atom (never re-def inside functions),
;; so callback assignment is stable under SlotRef lowering.
(def collision-callback* (atom nil))

^#^{:doc "Sets the global collision callback function. Pass nil to disable callback dispatch. Returns the assigned callback (or nil)."}
(def set-collision-callback!
  (fn set-collision-callback! [f]
    (if (or (nil? f) (fn? f))
      (reset! collision-callback* f)
      (throw "set-collision-callback! expects fn or nil"))))

^#^{:doc "Invokes the configured collision callback and returns its value. Returns nil when no callback is configured."}
(def invoke-collision-callback!
  (fn invoke-collision-callback! []
    (let [f (deref collision-callback*)]
      (if f
        (f)
        nil))))

)TINY_GFX_COL"

;; Tests for intentionally unsupported core.async state-machine macros.
;; These must throw (never silently no-op).

(do
  (require 'clojure.core.async)

  (let [assert-true (fn [x msg]
                      (when (not x)
                        (throw (str "ASSERT FAIL: " msg))))
        throws? (fn [thunk]
                  (try
                    (do (thunk) false)
                    (catch e true)))]

    (let [expanded-go (macroexpand-1 '(clojure.core.async/go 1))
          th-go (fn [] (eval expanded-go))]
      (assert-true (throws? th-go)
                   "go throws (unsupported)"))

    (let [th-go-loop (fn [] (clojure.core.async/go-loop [x 1] x))]
      (assert-true (throws? th-go-loop)
                   "go-loop throws (unsupported)"))

    (println "core_async go/go-loop unsupported: OK")))


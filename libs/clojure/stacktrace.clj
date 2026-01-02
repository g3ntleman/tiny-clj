(ns clojure.stacktrace)

;; Native hook (DEBUG builds only). In non-DEBUG/ESP32 builds this symbol exists,
;; but calling it will throw; stack-trace handles that and degrades to [].
^#^{:doc "Returns a stacktrace string for exception e (native/DEBUG builds). May throw in non-DEBUG builds."}
(defn stacktrace-str [e] :native)

^#^{:doc "Returns a sequence of stacktrace lines for exception e. Degrades to [] when stack traces are unavailable."}
(defn stack-trace [e]
  (let [s (try
            (stacktrace-str e)
            (catch Exception _ nil))]
    (if (nil? s)
      []
      [s])))

^#^{:doc "Prints a stack trace for exception e (one line per entry)."}
(defn print-stack-trace [e]
  (doseq [line (stack-trace e)]
    (println line))
  nil)

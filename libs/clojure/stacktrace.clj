(ns clojure.stacktrace)

;; Native hook. Returns nil when no native stacktrace is available.
^#^{:doc "Returns a stacktrace string for exception e, or nil when native stacktrace data is unavailable."}
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

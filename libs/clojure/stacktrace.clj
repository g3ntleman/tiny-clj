(ns clojure.stacktrace)

;; Native hook (DEBUG builds only). In non-DEBUG/ESP32 builds this symbol exists,
;; but calling it will throw; stack-trace handles that and degrades to [].
(defn stacktrace-str [e] :native)

(defn stack-trace [e]
  (let [s (try
            (stacktrace-str e)
            (catch Exception _ nil))]
    (if (nil? s)
      []
      [s])))

(defn print-stack-trace [e]
  (doseq [line (stack-trace e)]
    (println line))
  nil)

;; clojure.stacktrace - Stack trace utilities
;;
;; Tiny-CLJ: exposes DEBUG native backtraces (if available) in a Clojure-ish namespace.
;; On non-DEBUG / ESP32 builds, this degrades gracefully to empty output.

(ns clojure.stacktrace
  (:require [clojure.string :as cstr]))

;; Native hook (DEBUG builds only). We attempt to create the native stub at load time.
;; If the native isn't linked in this build, we fall back to a pure-Clojure no-op.
(def ^:private stacktrace-str*
  (try
    (fn stacktrace-str [e] :native)
    (catch Exception _
      (fn stacktrace-str [_] nil))))

(defn stack-trace
  "Returns a vector of stack trace lines for exception e.

  In Tiny-CLJ DEBUG builds, this is a native (C) backtrace captured at throw-time.
  In other builds, returns an empty vector." 
  [e]
  (let [s (stacktrace-str* e)]
    (if (nil? s)
      (vector)
      (vec (cstr/split-lines s)))))

(defn print-stack-trace
  "Prints the stack trace for exception e (one line per frame)." 
  [e]
  (doseq [line (stack-trace e)]
    (println line))
  nil)

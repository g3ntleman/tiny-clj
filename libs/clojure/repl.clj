;; clojure.repl - REPL helper functions
;; Provides utilities for interactive development

(ns clojure.repl
  (:require [clojure.core :as core :refer [meta if-let let when empty? doseq find-ns ns-map all-ns keys count]]
            [clojure.string :as cstr]))

;; ============================================================================
;; Documentation and Help Functions
;; ============================================================================

;; doc helpers
^#^{:doc "Normalizes a namespace value to its printable string form (or nil)."}
(defn normalize-ns-name [ns-val]
  (when ns-val
    (core/str ns-val)))

^#^{:doc "Internal helper to print documentation metadata map m in a Clojure-like format."}
(defn print-doc [m]
  (core/println "-------------------------")
  (let [ns-val (core/get m :ns)
        name-val (core/get m :name)
        ns-str (when ns-val (normalize-ns-name ns-val))]
    (when name-val
      (core/println (if ns-str
                      (core/str ns-str "/" name-val)
                      (core/str name-val)))))
  (let [forms (core/get m :forms)]
    (when forms
      (doseq [f forms]
        (core/print "  ")
        (core/prn f))))
  (let [arglists (core/get m :arglists)]
    (when arglists
      (core/prn arglists)))
  (cond
    (core/get m :special-form) (core/println "Special Form")
    (core/get m :macro) (core/println "Macro"))
  (let [doc (core/get m :doc)]
    (when doc
      (core/println (core/str " " doc)))))

;; doc - Print documentation for a var
;; Note: Requires metadata support
;; Format matches Clojure/JVM: 25 dashes, name, params (if available), doc
^#^{:doc "Prints documentation for x (var, function, or symbol). Uses metadata to show name, arglists, and docstring." :macro true}
(defn doc [x]
  (if-let [m (meta x)]
    (print-doc m)
    (core/println "No metadata available")))

;; source - Print source code for a function
;; Note: In Clojure, source is a normal function (not a special form)
;; Usage: (source 'function-name) or (source 'namespace/function-name)
^#^{:doc "Prints the source code for a function. Usage: (source 'function-name) or (source 'namespace/function-name)"}
(defn source [x] :native)

;; dir - List all public functions in a namespace
;; Format matches Clojure/JVM: just function names, one per line, no extra text
^#^{:doc "Prints all public vars in the namespace ns-name, one per line."}
(defn dir [& args]
  :native)

;; ============================================================================
;; Utility Functions
;; ============================================================================

;; find-doc - Find documentation matching a pattern across all namespaces
^#^{:doc "Searches docstrings in all loaded namespaces for the given pattern string."}
(defn find-doc [pattern]
  (if (nil? pattern)
    nil
    (let [pattern-str (core/str pattern)
          pattern-lower (cstr/lower-case pattern-str)
          search-ns (fn [ns-obj]
                      (let [ns-map (or (ns-map ns-obj) {})]
                        (doseq [[_ v] ns-map]
                          (let [m (meta v)]
                            (when m
                              (let [doc-str (core/get m :doc)]
                                (when doc-str
                                  (let [doc-lower (cstr/lower-case (core/str doc-str))]
                                    (when (cstr/includes? doc-lower pattern-lower)
                                      (doc v))))))))))]
      (doseq [ns-obj (all-ns)]
        (search-ns ns-obj))
      nil)))


;; ==========================================================================
;; Stacktraces
;; ==========================================================================

;; pst - Print stack trace
;; In JVM Clojure: (pst) prints stack trace for last exception *e.
;; Tiny-CLJ currently doesn't persist last exception, so (pst) prints a hint.
;; Use (pst e) inside a (catch Exception e ...) block.
^#^{:doc "Prints a stack trace. Use (pst e) inside (catch Exception e ...)."}
(defn pst
  [& args]
  (if (empty? args)
    (core/println "pst: no last exception (*e*) available; use (pst e) in (catch ...)")
    (let [e (core/first args)]
      ;; Load clojure.stacktrace only if pst is invoked.
      (require 'clojure.stacktrace)
      (clojure.stacktrace/print-stack-trace e))))


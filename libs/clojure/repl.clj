;; clojure.repl - REPL helper functions
;; Provides utilities for interactive development

(ns clojure.repl
  (:require [clojure.core :as core :refer [meta if-let let when empty? doseq find-ns ns-map all-ns keys count]]
            [clojure.string :as cstr]))

;; ============================================================================
;; Documentation and Help Functions
;; ============================================================================

;; doc helpers
(defn normalize-ns-name [ns-val]
  (when ns-val
    (core/str ns-val)))

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
^#^{:doc "Prints documentation for x (var, function, or symbol). Uses metadata to show name, arglists, and docstring."
    :macro true}
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

;; pst - Print stack trace (simplified)
;; Note: Full implementation requires:
;;   1. Exception persistence: Store last exception in *e variable
;;   2. Clojure-level stack traces: Map C functions to Clojure function names
;;   3. EvalState integration: Track function calls with namespace/name info
;;   4. Currently only C-level stack traces available (via backtrace)
;; In Clojure/JVM: Prints the stack trace of the last exception (*e)
^#^{:doc "Prints the last stack trace (placeholder implementation)."}
(defn pst []
  (println "Stack trace not yet fully implemented"))

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

;; rt - Return retain count of an object
^#^{:doc "Returns the reference count of an object as an integer. Usage: (rt obj)"}
(defn rt [x] :native)


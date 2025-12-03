;; clojure.repl - REPL helper functions
;; Provides utilities for interactive development

(ns clojure.repl
  (:require [clojure.core :refer [meta str println if-let let when empty? doseq find-ns ns-map get keys count]]
            [clojure.string :as str]))

;; ============================================================================
;; Documentation and Help Functions
;; ============================================================================

;; doc - Print documentation for a var
;; Note: Requires metadata support
;; Format matches Clojure/JVM: 25 dashes, name, params (if available), doc, 25 dashes
(defn doc [x]
  (if-let [m (meta x)]
    (let [name (or (get m :name) (:name m))
          doc-str (or (get m :doc) (:doc m))
          arglists (or (get m :arglists) (:arglists m))]
      (println "-------------------------")
      (when name
        (println (str name)))
      ;; Print parameter lists if available (like ([x]) or ([x y]))
      (when arglists
        (println (str arglists)))
      ;; Print documentation string if available
      (when doc-str
        (println (str "  " doc-str)))
      (println "-------------------------")
      nil)  ; Explicitly return nil (Clojure convention for side-effect functions)
    (do
      (println "No metadata available")
      nil)))  ; Explicitly return nil

;; source - Print source code for a function
;; Note: In Clojure, source is a normal function (not a special form)
;; Usage: (source 'function-name) or (source 'namespace/function-name)
^#^{:doc "Prints the source code for a function. Usage: (source 'function-name) or (source 'namespace/function-name)"}
(defn source [x] :native)

;; dir - List all public functions in a namespace
;; Format matches Clojure/JVM: just function names, one per line, no extra text
(defn dir [ns-name]
  (let [ns-obj (find-ns ns-name)
        ns-map (if ns-obj (ns-map ns-obj) {})]
    (if (empty? ns-map)
      nil
      (doseq [[k v] ns-map]
        (println k)))))

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
(defn pst []
  (println "Stack trace not yet fully implemented"))

;; find-doc - Find documentation matching a pattern (simplified)
;; Note: Full implementation would require access to all loaded namespaces
;; This version searches common namespaces: clojure.core, clojure.string, clojure.repl
;; In Clojure/JVM: Searches all loaded namespaces for doc strings containing pattern
(defn find-doc [pattern]
  (if (nil? pattern)
    nil
    (let [pattern-str (str pattern)
          pattern-lower (str/lower-case pattern-str)
          search-ns (fn [ns-name]
                      (let [ns-obj (find-ns ns-name)
                            ns-map (if ns-obj (ns-map ns-obj) {})]
                        (if (empty? ns-map)
                          nil
                          (doseq [[k v] ns-map]
                            (when-let [m (meta v)]
                              (when-let [doc-str (:doc m)]
                                (let [doc-lower (str/lower-case (str doc-str))]
                                  (when (str/includes? doc-lower pattern-lower)
                                    (doc v)))))))))]
      (search-ns 'clojure.core)
      (search-ns 'clojure.string)
      (search-ns 'clojure.repl)
      nil)))


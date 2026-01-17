;; clojure.pprint - minimal pretty printer for tiny-clj
;;
;; This is intentionally small and embedded-friendly:
;; - No width/column management
;; - No sorting
;; - Delegates to a native pretty string builder for low heap usage

(ns clojure.pprint
  (:require [clojure.core :as core]))

^#^{:doc "Returns a pretty-printed (readable) string representation of x.
This is a tiny-clj specific, embedded-friendly subset of clojure.pprint."}
(defn pprint-str [x] :native)

^#^{:doc "Pretty-prints x to *out* (via println) and returns nil."}
(defn pprint [x]
  (core/println (pprint-str x)))


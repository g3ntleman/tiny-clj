(ns req-test)

;; join - Joins collection with separator (copied from clojure.string - complex version)
^#^{:doc "Joins elements of coll into a string separated by separator (nil treated as \"\"). Returns \"\" for empty coll."}
(defn join [separator coll]
  (if (empty? coll)
    ""
    (let [sep (if (nil? separator) "" separator)
          build-list (fn [separator coll acc]
                       (if (empty? coll)
                         (if (empty? acc)
                           nil
                           (clojure.core/reverse acc))
                         (let [first-elem (first coll)
                               rest-coll (rest coll)]
                           (if (empty? rest-coll)
                             ;; Last element - add it without separator and finish
                             (if (empty? acc)
                               (list (str first-elem))
                               (clojure.core/reverse (cons (str first-elem) acc)))
                             ;; Not last element - add it with separator and recurse
                             (build-list separator rest-coll (cons separator (cons (str first-elem) acc)))))))
          concat-strings (fn [str-list]
                           (if (empty? str-list)
                             ""
                             (let [first-str (first str-list)
                                   rest-list (rest str-list)]
                               (if (empty? rest-list)
                                 first-str
                                 (let [next-result (concat-strings rest-list)]
                                   (str first-str next-result))))))]
      (let [result (build-list sep coll (list))]
        (if (nil? result)
          ""
          (concat-strings result)))))

;; Stub implementation of trim function without metadata
^#^{:doc "Stub implementation of trim; currently returns s unchanged."}
(defn trim [s]
  (if (or (nil? s) (= (count s) 0))
    s
    s))


;; clojure.string - String manipulation functions
;; Implements all 21 clojure.string functions
;; String-based operations work without TRE, Regex features require TRE

(ns clojure.string)

;; ============================================================================
;; Helper functions (used by other functions)
;; ============================================================================

;; Helper: Check if character is whitespace
;; Using character literals (now supported in tiny-clj)
(defn whitespace? [c]
  (or (= c \space)
      (= c \tab)
      (= c \newline)
      (= c \return)))

;; ============================================================================
;; Functions that work without TRE (19 functions)
;; ============================================================================

;; blank? - True if s is nil, empty, or contains only whitespace
(defn blank? [s]
  (if (nil? s)
    true
    (if (= (count s) 0)
      true
      (let [trimmed (clojure.string/trim s)]
        (= (count trimmed) 0)))))

;; capitalize - Converts first character to upper-case, rest to lower-case
(defn capitalize [s]
  (if (or (nil? s) (= (count s) 0))
    s
    (let [first-char (subs s 0 1)
          rest-str (if (> (count s) 1) (subs s 1) "")]
      (str (clojure.string/upper-case first-char) (clojure.string/lower-case rest-str)))))

;; ends-with? - True if s ends with substr
(defn ends-with? [s substr]
  (if (or (nil? s) (nil? substr))
    false
    (let [s-len (count s)
          substr-len (count substr)]
      (if (< s-len substr-len)
        false
        (let [last-idx (clojure.string/last-index-of s substr nil)]
          (if (nil? last-idx)
            false
            (= last-idx (- s-len substr-len))))))))

;; escape - Escapes characters using cmap
(defn escape [s cmap]
  (if (or (nil? s) (= (count s) 0))
    s
    (let [step (fn [s cmap acc idx]
                 (if (>= idx (count s))
                   (if (empty? acc)
                     nil
                     (clojure.core/reverse acc))
                   (let [c (subs s idx (+ idx 1))
                         replacement (get cmap c)]
                     (if (nil? replacement)
                       (step s cmap (cons c acc) (+ idx 1))
                       (step s cmap (cons (str replacement) acc) (+ idx 1))))))]
      (let [result (step s cmap (list) 0)]
        (if (nil? result)
          ""
          (clojure.string/join "" result))))))

;; includes? - True if s includes substr
(defn includes? [s substr]
  (not (nil? (index-of s substr nil))))

;; index-of - Returns index of value in s, optionally searching from from-index
(defn index-of [s value from-index]
  (if (or (nil? s) (nil? value))
    nil
    (let [s-len (count s)
          value-len (count value)
          start-idx (if (nil? from-index) 0 from-index)]
      (if (or (< s-len value-len) (< start-idx 0) (>= start-idx s-len))
        nil
        (let [step (fn [s value s-len value-len idx]
                     (if (> (+ idx value-len) s-len)
                       nil
                       (let [substr (subs s idx (+ idx value-len))]
                         (if (= substr value)
                           idx
                           (step s value s-len value-len (+ idx 1))))))]
          (step s value s-len value-len start-idx))))))

;; join - Joins collection with separator
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

;; join2 - Iterative implementation using recur (works without TCO)
(defn join2 [separator coll]
  (if (empty? coll)
    ""
    (let [sep (if (nil? separator) "" separator)
          build-list (fn [separator coll acc]
                       (let [step (fn [current-coll current-acc]
                                    (if (empty? current-coll)
                                      (if (empty? current-acc)
                                        nil
                                        (clojure.core/reverse current-acc))
                                      (let [first-elem (first current-coll)
                                            rest-coll (rest current-coll)]
                                        (if (empty? rest-coll)
                                          (step rest-coll (cons (str first-elem) current-acc))
                                          (step rest-coll (cons separator (cons (str first-elem) current-acc)))))))]
                         (step coll acc)))
          concat-strings (fn [str-list]
                           (let [step (fn [current-list acc]
                                        (if (empty? current-list)
                                          acc
                                          (let [first-str (first current-list)
                                                rest-list (rest current-list)]
                                            (step rest-list (str acc first-str)))))]
                             (step str-list "")))]
      (let [result (build-list sep coll (list))]
        (if (nil? result)
          ""
          (concat-strings result))))))

;; last-index-of - Returns last index of value in s
;; Note: from-index is optional, but we define it as required for now
^#^{:doc "Returns the index of the last occurrence of value in s, or nil if not found. Optionally takes from-index to start searching backwards from."}
(defn last-index-of [s value from-index] :native)

;; lower-case - Converts string to lower-case
^#^{:doc "Converts string to all lower-case."}
(defn lower-case [s] :native)

;; reverse - Reverses string
^#^{:doc "Returns s with its characters reversed."}
(defn reverse [s] :native)

;; starts-with? - True if s starts with substr
(defn starts-with? [s substr]
  (if (or (nil? s) (nil? substr))
    false
    (let [idx (index-of s substr 0)]
      (= idx 0))))

;; trim - Removes whitespace from both ends
^#^{:doc "Removes whitespace from both ends of string."}
(defn trim [s] :native)

;; triml - Removes whitespace from left
(defn triml [s]
  (if (or (nil? s) (= (count s) 0))
    s
    (let [trim-left (fn [s idx]
                      (if (>= idx (count s))
                        ""
                        (let [c (subs s idx (+ idx 1))
                              c-char (first c)]
                          (if (or (= c-char 32) (= c-char 9) (= c-char 10) (= c-char 13))
                            (trim-left s (+ idx 1))
                            (subs s idx)))))]
      (trim-left s 0))))

;; trimr - Removes whitespace from right
(defn trimr [s]
  (if (or (nil? s) (= (count s) 0))
    s
    (let [trim-right (fn [s idx]
                       (if (< idx 0)
                         ""
                         (let [c (subs s idx (+ idx 1))
                               c-char (first c)]
                           (if (or (= c-char 32) (= c-char 9) (= c-char 10) (= c-char 13))
                             (trim-right s (- idx 1))
                             (subs s 0 (+ idx 1))))))]
      (trim-right s (- (count s) 1)))))

;; trim-newline - Removes trailing newlines
(defn trim-newline [s]
  (if (or (nil? s) (= (count s) 0))
    s
    (let [trim-right (fn [s idx]
                       (if (< idx 0)
                         ""
                         (let [c (subs s idx (+ idx 1))
                               c-char (first c)]
                           (if (or (= c-char 10) (= c-char 13))
                             (trim-right s (- idx 1))
                             (subs s 0 (+ idx 1))))))]
      (trim-right s (- (count s) 1)))))

;; upper-case - Converts string to upper-case
^#^{:doc "Converts string to all upper-case."}
(defn upper-case [s] :native)

;; ============================================================================
;; Functions with String variants (work without TRE)
;; ============================================================================

;; split - Splits string on separator (String or Regex)
;; String version works without TRE
(defn split [s re limit]
  (if (nil? s)
    nil
    (if (= (count s) 0)
      (vector)
      ;; Check if re is a string (not a regex pattern)
      ;; For now, assume string separator
      (let [separator re
            lim (if (nil? limit) 0 limit)
            step (fn [s separator acc start-idx]
                   (let [idx (index-of s separator start-idx)]
                       (if (nil? idx)
                         ;; No more separators, add remaining string
                         (let [remaining (subs s start-idx)]
                           (if (and (> lim 0) (>= (count acc) (- lim 1)))
                             ;; Limit reached, add everything remaining
                             (let [last-part (str remaining (if (> start-idx 0) (subs s (- start-idx (count separator)) start-idx) ""))]
                               (clojure.core/reverse (cons last-part acc)))
                             (clojure.core/reverse (cons remaining acc))))
                         ;; Found separator
                         (if (and (> lim 0) (>= (count acc) (- lim 1)))
                           ;; Limit reached, add everything remaining
                           (let [last-part (subs s start-idx)]
                             (clojure.core/reverse (cons last-part acc)))
                           ;; Add part before separator and continue
                           (let [part (subs s start-idx idx)]
                             (step s separator (cons part acc) (+ idx (count separator))))))))]
        (let [result (step s separator (list) 0)]
          (if (nil? result)
            (vector)
            (vec result)))))))

;; replace - Replaces all instances of match with replacement
;; String version works without TRE
(defn replace [s match replacement]
  (if (or (nil? s) (nil? match))
    s
    (if (= (count s) 0)
      s
      ;; Check if match is a string (not a regex pattern)
      ;; For now, assume string match
      (let [step (fn [s match replacement acc start-idx]
                   (let [idx (index-of s match start-idx)]
                     (if (nil? idx)
                       ;; No more matches, add remaining string
                       (let [remaining (subs s start-idx)]
                         (if (empty? acc)
                           (if (= (count remaining) 0)
                             ""
                             remaining)
                           (clojure.string/join "" (clojure.core/reverse (cons remaining acc)))))
                       ;; Found match, replace it
                       (let [before (subs s start-idx idx)
                             after-start (+ idx (count match))]
                         (step s match replacement (cons replacement (cons before acc)) after-start)))))]
        (let [result (step s match replacement (list) 0)]
          (if (or (nil? result) (= (count result) 0))
            ""
            (if (string? result)
              result
              (clojure.string/join "" result))))))))

;; replace-first - Replaces first instance of match with replacement
;; String version works without TRE
(defn replace-first [s match replacement]
  (if (or (nil? s) (nil? match))
    s
    (if (= (count s) 0)
      s
      ;; Check if match is a string (not a regex pattern)
      ;; For now, assume string match
      (let [idx (index-of s match 0)]
        (if (nil? idx)
          ;; No match found, return original string
          s
          ;; Found match, replace it
          (let [before (subs s 0 idx)
                after-start (+ idx (count match))
                after (if (< after-start (count s)) (subs s after-start) "")]
            (str before replacement after)))))))

;; split-lines - Splits on newlines
(defn split-lines [s]
  (if (or (nil? s) (= (count s) 0))
    (vector)
    ;; Use split with "\n" and handle "\r\n"
    (let [normalized (replace s "\r\n" "\n")
          lines (split normalized "\n")]
      lines)))

;; ============================================================================
;; Functions that work without TRE (no regex needed)
;; ============================================================================

;; re-quote-replacement - Escapes replacement string
(defn re-quote-replacement [s]
  (if (or (nil? s) (= (count s) 0))
    s
    (let [step (fn [s acc idx]
                 (if (>= idx (count s))
                   (if (empty? acc)
                     ""
                     (join "" acc))
                   (let [c (subs s idx (+ idx 1))
                         c-char (first c)]
                     (if (or (= c-char 36) (= c-char 92))  ; $ = 36, \ = 92
                       (step s (cons "\\" (cons c acc)) (+ idx 1))
                       (step s (cons c acc) (+ idx 1))))))]
      (step s (list) 0))))

)

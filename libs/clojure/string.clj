;; clojure.string - String manipulation functions
;; Implements all 21 clojure.string functions
;; String-based operations work without TRE, Regex features require TRE

(ns clojure.string)

;; ============================================================================
;; Helper functions (used by other functions)
;; ============================================================================

;; Helper: Check if character is whitespace
;; Using character literals (now supported in tiny-clj)
^#^{:doc "Returns true if character c is whitespace (space, tab, newline, or return)."}
(defn whitespace? [c]
  (or (= c \space)
      (= c \tab)
      (= c \newline)
      (= c \return)))

;; ============================================================================
;; Functions that work without TRE (19 functions)
;; ============================================================================

;; blank? - True if s is nil, empty, or contains only whitespace
^#^{:doc "Returns true if s is nil, empty, or contains only whitespace."}
(defn blank? [s]
  (if (nil? s)
    true
    (if (= (count s) 0)
      true
      (let [trimmed (clojure.string/trim s)]
        (= (count trimmed) 0)))))

;; capitalize - Converts first character to upper-case, rest to lower-case
^#^{:doc "Returns s with its first character upper-cased and the rest lower-cased. Returns s unchanged if nil or empty."}
(defn capitalize [s]
  (if (or (nil? s) (= (count s) 0))
    s
    (let [first-char (subs s 0 1)
          rest-str (if (> (count s) 1) (subs s 1) "")]
      (str (clojure.string/upper-case first-char) (clojure.string/lower-case rest-str)))))

;; ends-with? - True if s ends with substr
^#^{:doc "Returns true if string s ends with substring substr."}
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
^#^{:doc "Returns a string where each character of s is replaced using cmap when present. cmap maps 1-character strings to replacement values."}
(defn escape [s cmap]
  (if (or (nil? s) (= (count s) 0))
    s
    (if (empty? cmap)
      s
      (let [s-len (count s)]
        (loop [idx 0
               out nil]
          (if (>= idx s-len)
            (if (nil? out) s out)
            (let [c (subs s idx (+ idx 1))
                  replacement (get cmap c)]
              (if (nil? replacement)
                (if (nil? out)
                  (recur (+ idx 1) nil)
                  (recur (+ idx 1) (str out c)))
                (if (nil? out)
                  (recur (+ idx 1) (str (subs s 0 idx) (str replacement)))
                  (recur (+ idx 1) (str out (str replacement))))))))))))

;; includes? - True if s includes substr
^#^{:doc "Returns true if s contains substr."}
(defn includes? [s substr]
  (not (nil? (last-index-of s substr nil))))

;; index-of - Returns index of value in s, optionally searching from from-index
^#^{:doc "Returns the index of value in s, or nil if not found. If from-index is provided, starts searching from that index."}
(defn index-of [s value from-index] :native)

;; join - Joins collection with separator
^#^{:doc "Joins the strings in coll, inserting separator between elements. Treats nil separator as \"\"."}
(defn join [separator coll]
  (if (empty? coll)
    ""
    (let [sep (if (nil? separator) "" separator)
          first-elem (first coll)
          rest-coll (rest coll)]
      (if (empty? rest-coll)
        (str first-elem)
        (str first-elem sep (join sep rest-coll))))))

;; join2 - Iterative implementation using recur (works without TCO)
^#^{:doc "Joins coll with separator like join, using an iterative strategy to avoid deep recursion."}
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

;; pad-left - Pads string on the left to given width
^#^{:doc "Returns s padded on the left with pad-char to width characters. If s is already >= width, returns s unchanged. pad-char should be a single-character string."}
(defn pad-left [s width pad-char] :native)

;; reverse - Reverses string
^#^{:doc "Returns s with its characters reversed."}
(defn reverse [s] :native)

;; starts-with? - True if s starts with substr
^#^{:doc "Returns true if string s starts with substring substr."}
(defn starts-with? [s substr]
  (if (or (nil? s) (nil? substr))
    false
    (let [idx (index-of s substr 0)]
      (= idx 0))))

;; trim - Removes whitespace from both ends
^#^{:doc "Removes whitespace from both ends of string s. Returns s with all leading and trailing whitespace removed. Whitespace includes space, tab, newline, and return characters. Returns the original string if s is nil or empty."}
(defn trim [s] :native)

;; triml - Removes whitespace from left
^#^{:doc "Removes leading whitespace from string s. Returns s unchanged if nil or empty."}
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
^#^{:doc "Removes trailing whitespace from string s. Returns s unchanged if nil or empty."}
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
^#^{:doc "Removes trailing newline characters (\\n and \\r) from string s."}
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
^#^{:doc "Splits string s on separator re (currently treated as a string). Optional limit behaves like Clojure's split: when >0, returns at most limit parts."}
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
^#^{:doc "Replaces all occurrences of match in s with replacement. Currently supports string match (no regex)."}
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
^#^{:doc "Replaces the first occurrence of match in s with replacement. Currently supports string match (no regex)."}
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
^#^{:doc "Splits string s into lines. Normalizes CRLF (\\r\\n) to LF (\\n) first."}
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
^#^{:doc "Escapes replacement string s for use in regex replacement contexts by prefixing $ and \\ with \\\\ (string-based implementation)."}
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

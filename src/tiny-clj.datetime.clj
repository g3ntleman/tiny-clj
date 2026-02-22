
R"DT(
(ns tiny-clj.datetime)

;; Ensure clojure.string utilities are available for formatting
(require 'clojure.string)

(defn dt-kw [name] (clojure.core/keyword "tiny-clj.datetime" name))

;; =============================================================================
;; Date/Time conversion library for tiny-clj
;;
;; The date algorithms (civil-from-days, days-from-civil) are based on:
;;
;;   Howard Hinnant's "chrono-Compatible Low-Level Date Algorithms"
;;   https://howardhinnant.github.io/date_algorithms.html
;;   Public Domain
;;
;; These algorithms are mathematically optimized for efficiency:
;; - No lookup tables
;; - No loops or branches for leap year calculation
;; - All intermediate values fit in 29-bit Fixnums
;;
;; Adapted for tiny-clj's integer constraints.
;; =============================================================================

;; Constants
(def unix-epoch-offset 719468)  ;; Days from 0000-03-01 to 1970-01-01
(def ms-per-day 86400000)
(def ms-per-hour 3600000)
(def ms-per-minute 60000)
(def ms-per-second 1000)

;; Qualified keys (interned once)
(def kw-year (dt-kw "year"))
(def kw-month (dt-kw "month"))
(def kw-day (dt-kw "day"))
(def kw-hour (dt-kw "hour"))
(def kw-minute (dt-kw "minute"))
(def kw-second (dt-kw "second"))
(def kw-millis (dt-kw "millis"))
(def kw-days (dt-kw "days"))
(def kw-ms (dt-kw "ms"))

;; Fixed-shape records for compact date/time payloads
(def civil-type 'tiny-clj.datetime/DateTimeCivil)
(def time-type 'tiny-clj.datetime/DateTimeTime)
(def raw-type 'tiny-clj.datetime/DateTimeRaw)

(record-register civil-type [kw-year kw-month kw-day])
(record-register time-type [kw-hour kw-minute kw-second kw-millis])
(record-register raw-type [kw-days kw-ms])

;; =============================================================================
;; Time conversion functions
;; =============================================================================

^#^{:doc "Converts milliseconds within a day (0-86399999) to a time map with qualified keys :tiny-clj.datetime/hour etc."}
(defn time-from-millis [ms]
  (let [hour   (quot ms ms-per-hour)
        minute (quot (mod ms ms-per-hour) ms-per-minute)
        second (quot (mod ms ms-per-minute) ms-per-second)
        millis (mod ms ms-per-second)]
    (record-create time-type [hour minute second millis])))

^#^{:doc "Converts hour, minute, second, millis to milliseconds within a day (0-86399999)"}
(defn millis-from-time [hour minute second millis]
  (+ (* hour ms-per-hour)
     (* minute ms-per-minute)
     (* second ms-per-second)
     millis))

;; =============================================================================
;; Civil date algorithms (Hinnant)
;; =============================================================================

^#^{:doc "Converts days since Unix epoch (1970-01-01) to a civil date map {:year y :month m :day d}. Based on Howard Hinnant's algorithm."}
(defn civil-from-days [unix-days]
  (let [;; z = days since March 1, 0000 (shifted epoch for easier math)
        z (+ unix-days unix-epoch-offset)
        ;; era = 400-year cycles since 0000
        era (quot (if (>= z 0) z (- z 146096)) 146097)
        ;; doe = day of era (0-146096)
        doe (- z (* era 146097))
        ;; yoe = year of era (0-399)
        yoe (quot (- doe (quot doe 1460) (- (quot doe 36524)) (quot doe 146096)) 365)
        ;; Year
        y (+ yoe (* era 400))
        ;; doy = day of year (0-365)
        doy (- doe (+ (* 365 yoe) (quot yoe 4) (- (quot yoe 100))))
        ;; mp = month adjusted (March=0, Feb=11)
        mp (quot (+ (* 5 doy) 2) 153)
        ;; Day of month (1-31)
        d (+ (- doy (quot (+ (* 153 mp) 2) 5)) 1)
        ;; Month (1-12)
        m (+ mp (if (< mp 10) 3 -9))
        ;; Adjust year for Jan/Feb
        y (if (<= m 2) (+ y 1) y)]
    (record-create civil-type [y m d])))

^#^{:doc "Converts year, month, day to days since Unix epoch (1970-01-01). Based on Howard Hinnant's algorithm."}
(defn days-from-civil [year month day]
  (let [;; Adjust year for Jan/Feb
        y (if (<= month 2) (- year 1) year)
        ;; era = 400-year cycles
        era (quot (if (>= y 0) y (- y 399)) 400)
        ;; yoe = year of era (0-399)
        yoe (- y (* era 400))
        ;; mp = month adjusted (March=0, Feb=11)
        mp (if (> month 2) (- month 3) (+ month 9))
        ;; doy = day of year
        doy (+ (quot (+ (* 153 mp) 2) 5) (- day 1))
        ;; doe = day of era
        doe (+ (* yoe 365) (quot yoe 4) (- (quot yoe 100)) doy)
        ;; Days since shifted epoch
        z (+ (* era 146097) doe)]
    ;; Subtract offset to get Unix days
    (- z unix-epoch-offset)))

;; =============================================================================
;; High-level API
;; =============================================================================

^#^{:doc "Converts a raw timestamp {:days d :ms m} to a full date-time map with qualified keys. Original :days and :ms are preserved."}
(defn date-time [raw]
  (let [raw-map (cond
                  ;; normalize map-like input (incl. records) to a persistent map
                  (and raw (map? raw)) (merge raw)
                  (inst? raw) (assoc {} kw-days (instant-days raw) kw-ms (instant-ms raw))
                  :else (let [raw-rec (to-raw raw)]
                          (assoc {} kw-days (get raw-rec kw-days)
                                   kw-ms   (get raw-rec kw-ms))))
        days (get raw-map kw-days)
        ms   (get raw-map kw-ms)
        c    (civil-from-days days)
        t    (time-from-millis ms)]
    (assoc raw-map
           kw-year   (get c kw-year)
           kw-month  (get c kw-month)
           kw-day    (get c kw-day)
           kw-hour   (get t kw-hour)
           kw-minute (get t kw-minute)
           kw-second (get t kw-second)
           kw-millis (get t kw-millis))))

^#^{:doc "Converts a date-time map back to a compact raw record with qualified keys :tiny-clj.datetime/days and :tiny-clj.datetime/ms. Uses existing :days/:ms if present, otherwise calculates from components."}
(defn to-raw [dt]
  (if (inst? dt)
    (record-create raw-type [(instant-days dt) (instant-ms dt)])
    (let [days   (get dt kw-days)
          ms     (get dt kw-ms)
          year   (get dt kw-year)
          month  (get dt kw-month)
          day    (get dt kw-day)
          hour   (get dt kw-hour)
          minute (get dt kw-minute)
          second (get dt kw-second)
          millis (get dt kw-millis)
          raw-days (or days (days-from-civil year month day))
          raw-ms   (or ms (millis-from-time hour minute second (or millis 0)))]
      (record-create raw-type [raw-days raw-ms]))))

;; =============================================================================
;; Formatting
;; =============================================================================

^#^{:doc "Formats a date-time map as ISO-8601 string like 2024-12-23T14:30:45. Expects qualified keys."}
(defn format-iso [m]
  (let [year   (get m kw-year)
        month  (get m kw-month)
        day    (get m kw-day)
        hour   (get m kw-hour)
        minute (get m kw-minute)
        second (get m kw-second)]
    (str (clojure.string/pad-left (str year) 4 "0") "-"
         (clojure.string/pad-left (str month) 2 "0") "-"
         (clojure.string/pad-left (str day) 2 "0") "T"
         (clojure.string/pad-left (str hour) 2 "0") ":"
         (clojure.string/pad-left (str minute) 2 "0") ":"
         (clojure.string/pad-left (str second) 2 "0"))))
)DT"

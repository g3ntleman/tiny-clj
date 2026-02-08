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

;; =============================================================================
;; Time conversion functions
;; =============================================================================

^#^{:doc "Converts milliseconds within a day (0-86399999) to a time map with qualified keys :tiny-clj.datetime/hour etc."}
(defn time-from-millis [ms]
  (assoc {} (dt-kw "hour")   (quot ms ms-per-hour)
             (dt-kw "minute") (quot (mod ms ms-per-hour) ms-per-minute)
             (dt-kw "second") (quot (mod ms ms-per-minute) ms-per-second)
             (dt-kw "millis") (mod ms ms-per-second)))

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
    (assoc {} (dt-kw "year") y (dt-kw "month") m (dt-kw "day") d)))

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
                  (and raw (map? raw)) raw
                  (inst? raw) (assoc {} (dt-kw "days") (instant-days raw) (dt-kw "ms") (instant-ms raw))
                  :else (to-raw raw))
        days (get raw-map (dt-kw "days"))
        ms   (get raw-map (dt-kw "ms"))]
    (merge raw-map
           (civil-from-days days)
           (time-from-millis ms))))

^#^{:doc "Converts a date-time map back to raw {:days :ms} format with qualified keys. Uses existing :days/:ms if present, otherwise calculates from components."}
(defn to-raw [dt]
  (let [days   (get dt (dt-kw "days"))
        ms     (get dt (dt-kw "ms"))
        year   (get dt (dt-kw "year"))
        month  (get dt (dt-kw "month"))
        day    (get dt (dt-kw "day"))
        hour   (get dt (dt-kw "hour"))
        minute (get dt (dt-kw "minute"))
        second (get dt (dt-kw "second"))
        millis (get dt (dt-kw "millis"))]
    (assoc {} (dt-kw "days") (or days (days-from-civil year month day))
             (dt-kw "ms")   (or ms (millis-from-time hour minute second (or millis 0))))))

;; =============================================================================
;; Formatting
;; =============================================================================

^#^{:doc "Formats a date-time map as ISO-8601 string like 2024-12-23T14:30:45. Expects qualified keys."}
(defn format-iso [m]
  (let [year   (get m (dt-kw "year"))
        month  (get m (dt-kw "month"))
        day    (get m (dt-kw "day"))
        hour   (get m (dt-kw "hour"))
        minute (get m (dt-kw "minute"))
        second (get m (dt-kw "second"))]
    (str (clojure.string/pad-left (str year) 4 "0") "-"
         (clojure.string/pad-left (str month) 2 "0") "-"
         (clojure.string/pad-left (str day) 2 "0") "T"
         (clojure.string/pad-left (str hour) 2 "0") ":"
         (clojure.string/pad-left (str minute) 2 "0") ":"
         (clojure.string/pad-left (str second) 2 "0"))))

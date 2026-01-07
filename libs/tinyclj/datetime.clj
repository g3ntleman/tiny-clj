(ns tinyclj.datetime)

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

^#^{:doc "Converts milliseconds within a day (0-86399999) to a time map {:hour h :minute m :second s :millis ms}"}
(defn time-from-millis [ms]
  {:hour   (quot ms ms-per-hour)
   :minute (quot (mod ms ms-per-hour) ms-per-minute)
   :second (quot (mod ms ms-per-minute) ms-per-second)
   :millis (mod ms ms-per-second)})

^#^{:doc "Converts hour, minute, second, millis to milliseconds within a day (0-86399999)"}
(defn millis-from-time [hour minute second millis]
  (+ (* hour ms-per-hour)
     (* minute ms-per-minute)
     (* second ms-per-second)
     millis))

;; =============================================================================
;; Civil date algorithms (native)
;; =============================================================================

;; Consolidated with native code in subjective-c/datetime_utc.c via :native stubs.

^#^{:doc "Converts days since Unix epoch (1970-01-01) to a civil date map {:year y :month m :day d}. Native implementation."}
(defn civil-from-days [unix-days] :native)

^#^{:doc "Converts year, month, day to days since Unix epoch (1970-01-01). Native implementation."}
(defn days-from-civil [year month day] :native)

;; =============================================================================
;; High-level API
;; =============================================================================

^#^{:doc "Converts a raw timestamp {:days d :ms m} to a full date-time map with :year :month :day :hour :minute :second :millis. Original :days and :ms are preserved."}
(defn date-time [t]
  (if (inst? t)
    (let [days (instant-days t)
          ms (instant-ms t)
          raw {:days days :ms ms}]
      (merge raw
             (civil-from-days days)
             (time-from-millis ms)))
    (throw (Exception. "date-time expects an Instant"))))

;; =============================================================================
;; Formatting
;; =============================================================================

^#^{:doc "Formats a date-time map as ISO-8601 string like 2024-12-23T14:30:45"}
(defn format-iso [_dt] :native)

;; GPIO smoke script for unit tests
(ns libs.test.gpio.smoke)

(require 'tiny-clj.gpio)

(tiny-clj.gpio/simulate! 1 1)
(tiny-clj.gpio/simulate-analog! 35 512)
(tiny-clj.gpio/read 1)
(tiny-clj.gpio/read-analog 35)

(let [w (tiny-clj.gpio/watch 35 (fn [_] nil) {:signal :analog :period-ms 1 :threshold 10})]
  (run-next-task)
  ((get w :close!)))

(println "[gpio/smoke] loaded")

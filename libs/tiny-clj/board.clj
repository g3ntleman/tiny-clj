(ns tiny-clj.board)

(def pins
  {1  {:kind :input :name :demo/launch :active :high}
   4  {:kind :output :name :tft/bl}
   5  {:kind :output :name :tft/cs}
   12 {:kind :input :name :right :pull :up :active :low}
   13 {:kind :input :name :a :pull :up :active :low}
   14 {:kind :input :name :left :pull :up :active :low}
   15 {:kind :input :name :y :pull :up :active :low}
   16 {:kind :output :name :tft/dc}
   17 {:kind :output :name :tft/rst}
   18 {:kind :output :name :tft/sclk}
   19 {:kind :input :name :x :pull :up :active :low}
   21 {:kind :piezo :name :piezo/melody :driver :ledc :voice 0}
   22 {:kind :piezo :name :piezo/sfx :driver :ledc :voice 1}
   23 {:kind :output :name :tft/mosi}
   25 {:kind :input :name :enc/sw :pull :up :active :low}
   26 {:kind :input :name :up :pull :up :active :low}
   27 {:kind :input :name :down :pull :up :active :low}
   32 {:kind :input :name :enc/a}
   33 {:kind :input :name :enc/b}
   34 {:kind :input :name :z :pull :ext :active :low}
   35 {:kind :adc :name :battery}})

(def buttons
  {:up {:pin 26 :pull :up :active :low :debounce-ms 20 :hold-ms 450}
   :down {:pin 27 :pull :up :active :low :debounce-ms 20 :hold-ms 450}
   :left {:pin 14 :pull :up :active :low :debounce-ms 20 :hold-ms 450}
   :right {:pin 12 :pull :up :active :low :debounce-ms 20 :hold-ms 450}

   :a {:pin 13 :pull :up :active :low :debounce-ms 20 :hold-ms 450}
   :ok {:pin 13 :pull :up :active :low :debounce-ms 20 :hold-ms 450}
   :fire {:pin 13 :pull :up :active :low :debounce-ms 20 :hold-ms 450}

   :back {:pin 19 :pull :up :active :low :debounce-ms 20 :hold-ms 450}
   :cancel {:pin 19 :pull :up :active :low :debounce-ms 20 :hold-ms 450}
   :x {:pin 19 :pull :up :active :low :debounce-ms 20 :hold-ms 450}
   :y {:pin 15 :pull :up :active :low :debounce-ms 20 :hold-ms 450}
   :z {:pin 34 :pull :ext :active :low :debounce-ms 20 :hold-ms 450}

   :demo/launch {:pin 1 :active :high :debounce-ms 20 :hold-ms 450}})

(def sensors
  {:battery {:pin 35
             :signal :analog
             :range [0 4095]
             :sample-period-ms 100
             :stable-ms 0
             :outlier-delta-max -1}
   :trigger {:pin 36
             :signal :analog
             :range [0 4095]
             :threshold 2400
             :hysteresis 80
             :stable-ms 15
             :outlier-delta-max 300
             :sample-period-ms 5}})

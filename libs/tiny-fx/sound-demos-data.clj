(ns tiny-fx.sound-demos-data)

(defn build-minuet-in-g-demo
  []
  {:track-id :sound-demos-minuet-in-g
   :steps [{:melody :D5 :backing [:G3] :duration :q}
           {:melody :G5 :backing [:D4] :duration :e}
           {:melody :A5 :backing [:D4] :duration :e}
           {:melody :B5 :backing [:G3] :duration :q}
           {:melody :G5 :backing [:D4] :duration :q}
           {:melody :D6 :backing [:G3] :duration :q}
           {:melody :B5 :backing [:D4] :duration :q}
           {:melody :G5 :backing [:G3] :duration :dq}
           {:melody :A5 :backing [:D4] :duration :e}
           {:melody :B5 :backing [:G3] :duration :q}
           {:melody :C6 :backing [:E4] :duration :q}
           {:melody :D6 :backing [:A3] :duration :q}
           {:rest :e}
           {:melody :B5 :backing [:D4] :duration :e}
           {:melody :A5 :backing [:G3] :duration :q}
           {:melody :G5 :backing [:D4] :duration :dh}]
   :opts {:melody {:volume 220}
          :backing {:volume 150}
          :tempo-bpm 140
          :gate-percent 78}})

(defn build-the-entertainer-demo
  []
  {:track-id :sound-demos-the-entertainer
   :steps [{:melody :D5 :backing [:G3] :duration :s}
           {:melody :E5 :backing [:D4] :duration :s}
           {:melody :C5 :backing [:G3] :duration :s}
           {:melody :A4 :backing [:D4] :duration :e}
           {:melody :B4 :backing [:G3] :duration :s}
           {:melody :G4 :backing [:G3] :duration :e}
           {:melody :D5 :backing [:G3] :duration :s}
           {:melody :E5 :backing [:D4] :duration :s}
           {:melody :C5 :backing [:G3] :duration :s}
           {:melody :A4 :backing [:D4] :duration :e}
           {:melody :B4 :backing [:G3] :duration :s}
           {:melody :G4 :backing [:G3] :duration :e}
           {:melody :E4 :backing [:C3] :duration :s}
           {:melody :C5 :backing [:G3] :duration :e}
           {:melody :E4 :backing [:E3] :duration :s}
           {:melody :C5 :backing [:G2] :duration :e}
           {:melody :E4 :backing [:C3] :duration :s}
           {:melody :C5 :backing [:C3] :duration :dq}
           {:melody :C6 :backing [:C4] :duration :s}
           {:melody :D6 :backing [:A3] :duration :s}
           {:melody :Ds6 :backing [:B3] :duration :s}
           {:melody :E6 :backing [:G2] :duration :s}
           {:melody :C6 :backing [:A3] :duration :s}
           {:melody :D6 :backing [:B3] :duration :s}
           {:melody :E6 :backing [:C3] :duration :e}
           {:melody :B5 :backing [:G2] :duration :s}
           {:melody :D6 :backing [:G3] :duration :e}
           {:melody :C6 :backing [:C3] :duration :dq}]
   :opts {:melody {:volume 220}
          :backing {:volume 128}
          :tempo-bpm 88
          :gate-percent 78}})

(defn build-gymnopedie-no-1-demo
  []
  {:track-id :sound-demos-gymnopedie-no-1
   :steps [{:melody :D5 :backing [:G2] :duration :dh}
           {:melody :E5 :backing [:D3] :duration :dh}
           {:melody :D5 :backing [:G2] :duration :dh}
           {:melody :B4 :backing [:D3] :duration :dh}
           {:melody :A4 :backing [:G2] :duration :h}
           {:rest :q}
           {:melody :B4 :backing [:D3] :duration :dh}
           {:melody :D5 :backing [:G2] :duration :dh}]
   :opts {:melody {:volume 210}
          :backing {:volume 120}
          :tempo-bpm 76
          :gate-percent 88}})

(defn build-rondo-alla-turca-demo
  []
  {:track-id :sound-demos-rondo-alla-turca
   :steps [{:melody :A5 :backing [:A3] :duration :s}
           {:melody :Gs5 :backing [:E4] :duration :s}
           {:melody :A5 :backing [:A3] :duration :s}
           {:melody :C6 :backing [:E4] :duration :s}
           {:melody :E6 :backing [:A3] :duration :e}
           {:melody :A5 :backing [:A3] :duration :s}
           {:melody :Gs5 :backing [:E4] :duration :s}
           {:melody :A5 :backing [:A3] :duration :s}
           {:melody :B5 :backing [:E4] :duration :s}
           {:melody :C6 :backing [:A3] :duration :e}
           {:melody :A5 :backing [:A3] :duration :s}
           {:melody :Gs5 :backing [:E4] :duration :s}
           {:melody :A5 :backing [:A3] :duration :s}
           {:melody :E5 :backing [:E4] :duration :s}
           {:melody :A5 :backing [:A3] :duration :e}
           {:rest :e}
           {:melody :G5 :backing [:G3] :duration :e}
           {:melody :A5 :backing [:E4] :duration :e}
           {:melody :B5 :backing [:G3] :duration :e}
           {:melody :C6 :backing [:A3] :duration :e}
           {:melody :E6 :backing [:A3] :duration :h}]
   :opts {:melody {:volume 220}
          :backing {:volume 142}
          :tempo-bpm 120
          :gate-percent 72}})

(defn build-hall-of-the-mountain-king-demo
  []
  {:track-id :sound-demos-hall-of-the-mountain-king
   :steps [{:melody :E4 :backing [:D3] :duration :q}
           {:melody :F4 :backing [:D3] :duration :q}
           {:melody :G4 :backing [:D3] :duration :q}
           {:melody :E4 :backing [:D3] :duration :q}
           {:melody :E4 :backing [:D3] :duration :q}
           {:melody :F4 :backing [:D3] :duration :q}
           {:melody :G4 :backing [:D3] :duration :q}
           {:melody :A4 :backing [:D3] :duration :q}
           {:melody :B4 :backing [:D3] :duration :q}
           {:rest :e}
           {:melody :A4 :backing [:D3] :duration :h}
           {:melody :E4 :backing [:D3] :duration :w}]
   :opts {:melody {:volume 220}
          :backing {:volume 138}
          :tempo-bpm 60
          :gate-percent 80}})

(defn build-can-can-demo
  []
  {:track-id :sound-demos-can-can
   :steps [{:melody :A5 :backing [:A3] :duration :e}
           {:melody :C6 :backing [:E4] :duration :e}
           {:melody :A5 :backing [:A3] :duration :e}
           {:melody :G5 :backing [:E4] :duration :e}
           {:melody :E5 :backing [:A3] :duration :e}
           {:melody :G5 :backing [:E4] :duration :e}
           {:melody :E5 :backing [:A3] :duration :e}
           {:melody :D5 :backing [:E4] :duration :e}
           {:melody :C5 :backing [:A3] :duration :e}
           {:melody :E5 :backing [:E4] :duration :e}
           {:melody :C5 :backing [:A3] :duration :e}
           {:melody :B4 :backing [:E4] :duration :e}
           {:melody :A4 :backing [:A3] :duration :q}
           {:rest :q}]
   :opts {:melody {:volume 220}
          :backing {:volume 152}
          :tempo-bpm 120
          :gate-percent 76}})

(defn build-rocket-launch-sfx-demo
  []
  {:track-id :sound-demos-rocket-launch-sfx
   :steps [{:notes [180] :bend [285] :noise true :duration 1100}
           {:notes [220] :bend [345] :noise true :duration 1100}
           {:notes [560] :duration 80}]
   :opts {:channel-count 1
          :volumes [240]
          :gate-percent 100}})

(defn build-laser-sfx-demo
  []
  {:track-id :sound-demos-laser
   :steps [{:notes [5200] :bend [1200] :duration 160}
           {:notes [550] :duration 56}]
   :opts {:channel-count 1
          :volumes [228]
          :gate-percent 88}})

(defn build-demo
  [which]
  (if (= which :minuet-in-g)
    (build-minuet-in-g-demo)
    (if (= which :the-entertainer)
      (build-the-entertainer-demo)
      (if (= which :gymnopedie-no-1)
        (build-gymnopedie-no-1-demo)
        (if (= which :rondo-alla-turca)
          (build-rondo-alla-turca-demo)
          (if (= which :hall-of-the-mountain-king)
            (build-hall-of-the-mountain-king-demo)
            (if (= which :can-can)
              (build-can-can-demo)
              (if (= which :rocket-launch-sfx)
                (build-rocket-launch-sfx-demo)
                (if (= which :laser-sfx)
                  (build-laser-sfx-demo)
                  nil)))))))))

(defn sfx-demo-key?
  [which]
  (or (= which :rocket-launch-sfx)
      (= which :laser-sfx)))

(ns fx)

(defn sweep-aabb
  "Predictive CCD sweep-AABB collision query.

  mover     - map with :x :y :w :h (top-left origin, pixel coords)
  vel       - map with :vx :vy (pixels per ms)
  obstacles - seq of maps, each with :id :x :y :w :h (nil → no-hit)
  max-ms    - integer time window (ms)

  Returns a SweepHit record {:hit-id N :time-ms N :normal kw} where :normal is
  one of :left :right :top :bottom, or nil when no collision occurs within max-ms."
  [mover vel obstacles max-ms]
  :native)

(defn interpolate-segment
  "Linearly interpolates a motion segment at the given wall-clock time.

  seg    - map with :start-ms :end-ms :from-x :from-y :to-x :to-y
  now-ms - current time in ms

  Returns {:x N :y N} clamped to [:from-x/:from-y … :to-x/:to-y]."
  [seg now-ms]
  :native)

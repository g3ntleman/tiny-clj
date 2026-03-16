(ns tiny-clj.gpio)

;; GPIO API for tiny-clj.
;;
;; Low-level pin I/O, PWM, and analog primitives (native-backed) plus
;; higher-level helpers such as set-pin-mode! and gpio-channel.

^#^{:doc "Registers or removes a GPIO edge-interrupt watcher.

  (watch pin callback)  ; register watcher
  (watch pin nil)       ; remove watcher for pin

Event maps delivered to callback:
  {:source :gpio, :signal :digital, :kind :edge, :pin <fixnum>, :value <0|1>}

Returns nil. Throws on invalid pin or non-callable callback."}
(defn- watch-native [pin f] :native)

^#^{:doc "Host/test helper: simulates a GPIO pin level change.

  (simulate! pin value)

Enqueues an edge event for any watcher on pin. No-op on ESP32.
Returns nil."}
(defn simulate! [pin value] :native)

^#^{:doc "Sets a digital output level on a GPIO pin.

  (write! pin level)

level: 0 = low, non-zero = high. On ESP32, implicitly configures
the pin as output. Returns nil."}
(defn write! [pin level] :native)

^#^{:doc "Reads the digital level of a GPIO pin.

  (read pin)  ;=> 0 or 1

Returns fixnum 0 (low) or 1 (high)."}
(defn read [pin] :native)

^#^{:doc "Reads an analog GPIO/ADC pin as a raw 12-bit value.

  (read-analog pin)  ;=> 0..4095

Returns a raw ADC fixnum in the range 0..4095.
On host, returns the last value set by simulate-analog!.
On ESP32, reads the pin through the ADC oneshot driver."}
(defn read-analog [pin] :native)

^#^{:doc "Host/test helper: simulates a raw analog GPIO/ADC reading.

  (simulate-analog! pin value)

Stores a raw ADC value (0..4095) for later read-analog calls.
No-op on ESP32. Returns nil."}
(defn simulate-analog! [pin value] :native)

^#^{:doc "Configures PWM output on a GPIO pin.

  (pwm! pin freq-hz duty)

freq-hz: frequency in Hz (>= 1). duty: 0..255 (8-bit resolution).
On ESP32 uses LEDC peripheral. Prefer set-pin-mode! for high-level API.
Returns nil."}
(defn pwm! [pin freq-hz duty] :native)

^#^{:doc "Stops PWM output on a GPIO pin and drives it low.

  (pwm-stop! pin)

On ESP32, releases the LEDC channel binding.
Prefer (set-pin-mode! pin nil) for high-level API.
Returns nil."}
(defn pwm-stop! [pin] :native)

;; --- High-level helpers ------------------------------------------------

(def ^:private core-async-loaded? (atom false))

^#^{:doc "Digital high output level constant (1). Prefer with pin-write."}
(def HIGH 1)

^#^{:doc "Digital low output level constant (0). Prefer with pin-write."}
(def LOW 0)

(defn- ensure-core-async! []
  (if @core-async-loaded?
    nil
    (do
      (require 'clojure.core.async)
      (reset! core-async-loaded? true)
      nil)))


^#^{:doc "Internal helper for the polling-based analog watch path."}
(defn- watch-analog [pin callback opts]
  (let [opts (if (nil? opts) {} opts)
        period-ms (let [v (get opts :period-ms)] (if (nil? v) 50 v))
        threshold (let [v (get opts :threshold)] (if (nil? v) 0 v))
        emit-initial? (let [v (get opts :emit-initial?)] (if (nil? v) true v))
        state (atom {:initialized? false :last 0})
        timer-id (schedule-periodic
                   0
                   period-ms
                   (fn []
                     (let [value (read-analog pin)
                           snapshot @state
                           initialized? (get snapshot :initialized?)
                           last (get snapshot :last)
                           delta (if initialized? (abs (- value last)) 0)
                           should-emit (if initialized?
                                         (>= delta threshold)
                                         emit-initial?)]
                       (reset! state
                               (if initialized?
                                 (if should-emit
                                   {:initialized? true :last value}
                                   snapshot)
                                 {:initialized? true :last value}))
                       (if should-emit
                         (callback {:source :gpio
                                    :signal :analog
                                    :kind :analog
                                    :pin pin
                                    :value value
                                    :delta delta})
                         nil))))]
    {:pin pin
     :timer-id timer-id
     :close! (fn []
               (cancel-timer timer-id))}))

^#^{:doc "Registers a GPIO watcher.

  (watch pin callback)                       ; digital watch (default)
  (watch pin callback {:signal :digital})    ; explicit digital watch
  (watch pin callback {:signal :analog ...}) ; analog polling watch
  (watch pin nil)                            ; remove digital watch

Digital events:
  {:source :gpio, :signal :digital, :kind :edge, :pin <fixnum>, :value <0|1>}

Analog events:
  {:source :gpio, :signal :analog, :kind :analog, :pin <fixnum>, :value <0..4095>, :delta <fixnum>}

Digital watch returns nil. Analog watch returns
{:pin pin, :timer-id timer-id, :close! (fn [])}.

Analog removal stays explicit through the returned handle's :close! function."}
(defn watch [pin callback & args]
  (let [raw-opts (first args)
        opts (if (nil? raw-opts) {} raw-opts)
        signal (let [v (get opts :signal)] (if (nil? v) :digital v))]
    (cond
      (= signal :digital)
      (watch-native pin callback)

      (= signal :analog)
      (do
        (when (nil? callback)
          (throw "watch: analog watches require a callback; close the returned handle explicitly"))
        (watch-analog pin callback opts))

      :else
      (throw (str "watch: unsupported :signal " signal)))))

^#^{:doc "Configures a GPIO pin mode.

  (set-pin-mode! pin :pwm {:freq 1000 :duty 128})  ; configure PWM output
  (set-pin-mode! pin :input)                        ; configure as digital input
  (set-pin-mode! pin :input {:pull :up})            ; input with pull-up (ESP32)
  (set-pin-mode! pin :output)                       ; configure as digital output
  (set-pin-mode! pin nil)                           ; release pin / stop PWM

Analog reads stay separate via read-analog and watch with {:signal :analog}.

Supported modes:
  :pwm    - requires opts map with :freq (Hz) and :duty (0..255)
  :input  - digital input (opts {:pull :up|:down} reserved for ESP32)
  :adc    - analog input
  :dac    - analog output via pin-write with raw values 0..255
  :output - digital output (use write! for level)
  nil     - release pin configuration, stops PWM if active

Returns nil. Throws on unknown mode or missing required opts."}
(defn set-pin-mode! [pin mode & args] :native)

^#^{:doc "Returns the configured semantic mode entry for a pin.

  (pin-mode pin)  ;=> {:mode :output} or nil

Returns the stored pin-mode map, or nil if the pin is currently
unconfigured."}
(defn pin-mode [pin] :native)

^#^{:doc "Reads a pin value using the public GPIO naming scheme.

  (pin-read pin)

Dispatches by configured semantic mode:
  :input -> digital read
  :adc   -> analog read

Throws if no mode was configured first."}
(defn pin-read [pin] :native)

^#^{:doc "Writes a pin value using the public GPIO naming scheme.

  (pin-write pin HIGH)
  (pin-write pin LOW)

Dispatches by configured semantic mode:
  :output -> digital write
  :dac    -> raw DAC write (0..255)

Throws if no mode was configured first. Digital writes prefer HIGH/LOW."}
(defn pin-write [pin value] :native)

^#^{:doc "Configures PWM output using the public GPIO naming scheme.

  (pin-pwm! pin freq-hz duty)

Equivalent to the low-level PWM primitive with the public naming scheme."}
(defn pin-pwm! [pin freq-hz duty] :native)

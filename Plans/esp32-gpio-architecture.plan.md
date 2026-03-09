---
name: "ESP32 GPIO Architecture"
overview: "Current-state plan for the GPIO stack after the namespace migration to tiny-clj.gpio, embedded-source integration, and API consolidation."
last_validated: "2026-03-09"
todos:
  - id: gpio-namespace-runtime
    content: "Keep GPIO primitives out of clojure.core and maintain tiny-clj.gpio as the public hardware namespace."
    status: completed
  - id: gpio-embedded-source
    content: "Keep the full tiny-clj.gpio namespace embedded via src/tiny-clj.gpio.clj and src/embedded_sources.c for ESP32 builds."
    status: completed
  - id: gpio-api-docs
    content: "Maintain docstrings and Doxygen for watch/simulate!/write!/read/pwm!/pwm-stop!/gpio-channel/gpio-mode!."
    status: completed
  - id: gpio-analog-read
    content: "Add analog input API (gpio-read-analog or adc-read) with ESP32 ADC backend and host contract tests."
    status: pending
  - id: gpio-analog-threshold
    content: "Add optional threshold-based analog callbacks or polling helpers to suppress noisy value changes."
    status: pending
  - id: gpio-board-validation
    content: "Run ESP32 board validation for edge callbacks, PWM output, and runtime drop counters."
    status: pending
  - id: gpio-builtins-split
    content: "Optionally move GPIO native registration out of src/builtins.c into a dedicated src/builtins_gpio.c for consistency with other builtin groups."
    status: pending
---

# ESP32 GPIO Architecture

## Current State

- `clojure.core` is standard library only. GPIO primitives no longer live there.
- The public hardware namespace is `tiny-clj.gpio`.
- The full namespace is available both as a normal library file in `libs/tiny-clj/gpio.clj` and as an embedded source in `src/tiny-clj.gpio.clj`.
- `src/embedded_sources.c` registers `/libs/tiny-clj/gpio.clj`, so ESP32 builds can always load the GPIO namespace from embedded sources.
- All ESP32 targets have GPIO pins, so the GPIO namespace does not need a feature gate.
- Native lookup now uses qualified symbols in `src/builtins.c`:
  - `tiny-clj.gpio/watch`
  - `tiny-clj.gpio/simulate!`
  - `tiny-clj.gpio/write!`
  - `tiny-clj.gpio/read`
  - `tiny-clj.gpio/pwm!`
  - `tiny-clj.gpio/pwm-stop!`
- Backend implementations remain platform-specific:
  - Host: `src/gpio_host.c`
  - ESP32: `src/gpio_esp32.c`

## Public API

| Function | Role | Notes |
|---|---|---|
| `watch` | Register or remove edge callbacks | `(watch pin nil)` removes the watcher; `gpio-unwatch` no longer exists |
| `simulate!` | Host/test helper | Generates synthetic GPIO events on host; no-op on ESP32 |
| `write!` | Digital output primitive | Small native primitive for low-overhead output |
| `read` | Digital input primitive | Returns `0` or `1` |
| `pwm!` | Low-level PWM primitive | Uses ESP32 LEDC backend |
| `pwm-stop!` | Stop PWM on a pin | Releases LEDC binding and drives low |
| `gpio-channel` | core.async wrapper | Ergonomic channel-based event delivery on top of `watch` |
| `gpio-mode!` | High-level pin-mode helper | Wraps low-level primitives and keeps mode dispatch in Clojure |

## Architecture Rules

1. Keep `clojure.core` limited to standard library semantics.
2. Put hardware primitives in dedicated namespaces such as `tiny-clj.gpio`.
3. Keep native primitives small and predictable; compose higher-level behavior in Clojure.
4. Keep the embedded namespace and the library namespace aligned so host and ESP32 load the same public API.
5. Keep ISR work minimal: ring-buffer push plus drain request flag only; callback execution stays in event-loop context.

## Implemented and Verified

### Namespace and loading model

- GPIO functions were moved out of `src/clojure.core.clj`.
- `src/tiny-clj.gpio.clj` now embeds the complete GPIO namespace, including:
  - native-backed low-level functions
  - `gpio-channel`
  - `gpio-mode!`
- `src/embedded_sources.c` registers the GPIO namespace alongside the other embedded libraries.

### Native lookup and tests

- `src/builtins.c` now exposes qualified native entries for `tiny-clj.gpio/*`.
- `src/tests/test_gpio_write.c` was updated to require `tiny-clj.gpio` and assert qualified symbol lookup and invocation.
- `src/tests/test_vector_scene_graph.c` uses `tiny-clj.gpio/simulate!` for the game-demo GPIO integration test.

### API consolidation

- Watcher registration and removal are consolidated into `watch`.
- The old split `gpio-watch` / `gpio-unwatch` API is gone.
- The public names inside the namespace intentionally avoid a redundant `gpio-` prefix for low-level operations.

### Documentation

- Public Clojure functions in `tiny-clj.gpio` have docstrings.
- Native C implementations in `src/gpio_host.c` and `src/gpio_esp32.c` have Doxygen comments.

## Performance Constraints

### Implemented

- ISR-side enqueueing was replaced with a flag-only bridge plus thread-context draining.
- Runtime stats expose GPIO event drops.
- The ESP32 run loop uses an adaptive sleep strategy to reduce timer and GPIO latency.

### Why this still matters

- GPIO callbacks are part of the game/runtime hot path.
- Event overflow must remain visible through telemetry.
- The callback-first API should remain the default for low-latency input; `gpio-channel` is a convenience layer, not the fast path.

## Remaining Work

### Analog input

- Add `gpio-read-analog` or `adc-read`.
- Define the return contract clearly:
  - raw ADC value
  - or millivolts
- Add host-side contract tests and ESP32 ADC backend coverage.

### Analog change filtering

- Add an optional threshold to suppress small analog changes.
- Keep the filtering semantics explicit: only deliver updates when `abs(new - last) >= threshold`.

### Board validation

- Validate on real ESP32 hardware:
  - edge callback delivery
  - PWM output start/stop
  - event-drop counters under burst input

### Optional cleanup

- If builtin registration keeps growing, move GPIO-native registration from `src/builtins.c` into `src/builtins_gpio.c`.
- This is a structural cleanup only; the runtime contract should stay unchanged.

## Example

```clojure
(require 'tiny-clj.gpio)

(defonce led-state (atom 0))

(defn toggle-led! [pin]
  (let [next (if (zero? @led-state) 1 0)]
    (reset! led-state next)
    (tiny-clj.gpio/write! pin next)))

(schedule-periodic
  0
  500
  {:id :status-led
   :fn (fn [] (toggle-led! 2))})
```

## Verification Snapshot

- Verified host GPIO test set: 24 tests passing.
- Verified integration checks:
  - `test_vector_scene_graph_game_demo_gpio_press_triggers_demo_melody_once`
  - `test_plan_trackA_gpio_smoke_script`
- Full suite status at validation time: one unrelated sound test failure remained outside the GPIO scope.

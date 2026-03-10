---
name: "ESP32 GPIO Architecture"
overview: "Current GPIO architecture and API direction for tiny-clj.gpio after namespace migration, embedded-source integration, analog input support, builtin split-out, and the new recommendation to converge on one public watch API."
last_validated: "2026-03-10"
todos:
  - id: gpio-namespace-runtime
    content: "Keep GPIO primitives out of clojure.core and maintain tiny-clj.gpio as the public hardware namespace."
    status: completed
  - id: gpio-embedded-source
    content: "Keep the full tiny-clj.gpio namespace embedded via src/tiny-clj.gpio.clj and src/embedded_sources.c for ESP32 builds."
    status: completed
  - id: gpio-api-docs
    content: "Maintain docstrings and Doxygen for watch/simulate!/pin-write/pin-read/pin-pwm!/pwm-stop!/gpio-channel/set-pin-mode!/pin-mode plus digital level symbols."
    status: completed
  - id: gpio-analog-read
    content: "Add analog input API (gpio-read-analog or adc-read) with ESP32 ADC backend and host contract tests."
    status: completed
  - id: gpio-analog-threshold
    content: "Add optional threshold-based analog callbacks or polling helpers to suppress noisy value changes."
    status: completed
  - id: gpio-watch-api-unification
    content: "Converge on one public watch API that stays minimal and generic while keeping separate digital and analog implementation paths internally."
    status: completed
  - id: gpio-pin-mode-state
    content: "Introduce an internal pin-modes* atom as the source of truth for configured pin semantics instead of relying on hardware introspection."
    status: completed
  - id: gpio-c-api-consolidation
    content: "Consolidate the native GPIO/LEDC API in src/gpio.c so GPIO builtins and the sound backend share one C core instead of owning separate LEDC logic."
    status: completed
  - id: gpio-board-validation
    content: "Run ESP32 board validation for edge callbacks, PWM output, and runtime drop counters."
    status: pending
  - id: gpio-watch-eval-map-regression
    content: "Fix evaluator handling for map/set literals passed as function arguments so local symbols are resolved correctly in analog watch event maps."
    status: completed
  - id: gpio-watch-regression-tests
    content: "Keep focused regression coverage for map-literal callback arguments, watch analog callback delivery, and timer-driven event maps without def-based workarounds."
    status: completed
  - id: gpio-watch-remove-def-hacks
    content: "Remove temporary def-based watch-analog test workarounds after the evaluator bug is fixed."
    status: completed
  - id: gpio-builtins-split
    content: "Optionally move GPIO native registration out of src/builtins.c into a dedicated src/builtins_gpio.c for consistency with other builtin groups."
    status: completed
  - id: gpio-pin-write-dac
    content: "Implement mode-sensitive pin-write dispatch for :dac through the shared GPIO C core on host and ESP32."
    status: completed
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
  - `tiny-clj.gpio/read-analog`
  - `tiny-clj.gpio/pwm!`
  - `tiny-clj.gpio/pwm-stop!`
  - `tiny-clj.gpio/simulate-analog!`
- Backend implementations remain platform-specific:
  - Host: `src/gpio_host.c`
  - ESP32: `src/gpio_esp32.c`
- A shared runtime-facing GPIO core now exists in:
  - `src/gpio.c`
  - `src/gpio.h`
- The first consolidation step is in place:
  - host watcher/level runtime state moved out of `src/gpio_host.c` into `src/gpio.c`
  - `src/gpio_host.c` now focuses on host-specific behavior instead of owning shared runtime maps
- Native builtin entry points now live in `src/gpio.c` and dispatch into backend helpers.
- `src/gpio.c` now also exposes a direct shared C API for runtime callers:
  - watch set/clear
  - digital write/read
  - analog read
  - PWM start/stop
  - simulation helpers
  - GPIO event-drain polling and drop-counter access
- `src/gpio_host.c` and `src/gpio_esp32.c` no longer own the `native_gpio_*` builtin signatures directly.
- `src/sound_backend_esp32.c` now routes buzzer PWM output through the shared ESP32 GPIO PWM backend instead of programming LEDC directly.
- `src/sound_backend_esp32.c`, `src/event_loop.c`, `src/builtins.c`, and host viewer input simulation now use `src/gpio.c` / `src/gpio.h` as the runtime-facing GPIO layer instead of calling ESP32 backend helpers directly.
- The public high-level GPIO naming layer is now in place:
  - `set-pin-mode!`
  - `pin-mode`
  - `pin-read`
  - `pin-write`
  - `pin-pwm!`
  - `HIGH` / `LOW`
- Public watch behavior is now unified:
  - `watch` is the public entry point
  - digital watching remains the default path
  - analog watching uses `{:signal :analog ...}` and remains polling-based internally
  - watch event maps now include `:signal` as an explicit digital/analog discriminator

## Public API

| Function | Role | Notes |
|---|---|---|
| `watch` | Register or remove watch callbacks | Unified public watch API; digital by default, analog via `{:signal :analog ...}` |
| `simulate!` | Host/test helper | Generates synthetic GPIO events on host; no-op on ESP32 |
| `write!` | Digital output primitive | Current low-level API; target public generic name is `pin-write` |
| `read` | Digital input primitive | Current low-level API; target public generic name is `pin-read` |
| `read-analog` | Low-level analog input primitive | Returns raw 12-bit ADC values `0..4095` |
| `simulate-analog!` | Host/test analog helper | Seeds host-side analog readings for tests and smoke scripts |
| `pwm!` | Low-level PWM primitive | Current low-level API; target public generic name is `pin-pwm!` |
| `pwm-stop!` | Stop PWM on a pin | Releases LEDC binding and drives low |
| `gpio-channel` | core.async wrapper | Ergonomic channel-based delivery for digital watch events today; can stay as a convenience layer |
| `set-pin-mode!` | High-level pin-mode setter | Configures semantic pin mode and keeps mode dispatch in Clojure |
| `pin-mode` | Pin-mode introspection | Returns the configured semantic mode entry for one pin |
| `HIGH` / `LOW` | Digital level symbols | Preferred readable values for `pin-write` on digital output pins |

## Architecture Rules

1. Keep `clojure.core` limited to standard library semantics.
2. Put hardware primitives in dedicated namespaces such as `tiny-clj.gpio`.
3. Keep native primitives small and predictable; compose higher-level behavior in Clojure.
4. Keep the embedded namespace and the library namespace aligned so host and ESP32 load the same public API.
5. Keep ISR work minimal: ring-buffer push plus drain request flag only; callback execution stays in event-loop context.
6. Favor one minimal public watcher abstraction where possible; keep distinct backend paths when delivery semantics differ.

## Implemented and Verified

### Namespace and loading model

- GPIO functions were moved out of `src/clojure.core.clj`.
- `src/tiny-clj.gpio.clj` now embeds the complete GPIO namespace, including:
  - native-backed low-level functions
  - `read-analog`
  - `simulate-analog!`
  - `gpio-channel`
  - unified `watch`
  - `set-pin-mode!`
  - `pin-mode`
  - `pin-read`
  - `pin-write`
  - `pin-pwm!`
  - `HIGH` / `LOW`
- `src/embedded_sources.c` registers the GPIO namespace alongside the other embedded libraries.

### Native lookup and tests

- GPIO-native lookup was split out into `src/builtins_gpio.c` and `src/builtins_gpio.h`.
- `src/builtins.c` now delegates GPIO symbol resolution to `builtins_gpio_native_function_lookup()`.
- `src/tests/test_gpio_write.c` was updated to require `tiny-clj.gpio` and assert qualified symbol lookup and invocation.
- `src/tests/test_vector_scene_graph.c` uses `tiny-clj.gpio/simulate!` for the game-demo GPIO integration test.
- `src/tests/test_embedded_sources.c` verifies that `/libs/tiny-clj/gpio.clj` is embedded.
- A shared GPIO runtime layer was started in `src/gpio.c` / `src/gpio.h`.
- Host-only runtime state such as simulated pin levels and watcher maps no longer lives in `src/gpio_host.c`.
- `src/gpio.c` now owns the `native_gpio_*` builtin implementations and shared argument-parsing dispatch.
- `src/gpio_host.c` and `src/gpio_esp32.c` now expose backend-oriented helper functions.
- `src/sound_backend_esp32.c` now reuses the shared GPIO PWM backend for voice output.
- The surrounding runtime code now consumes GPIO through `src/gpio.h` rather than including `gpio_esp32.h` directly, except for backend-only call sites such as the ESP32 event ingress implementation itself.

### API consolidation

- Watcher registration and removal are consolidated into `watch`.
- The old split `gpio-watch` / `gpio-unwatch` API is gone.
- The public names inside the namespace intentionally avoid a redundant `gpio-` prefix for low-level operations.
- `watch` is now the single public watch entry point.
- The analog polling implementation still exists internally as a helper behind `watch`.

### Documentation

- Public Clojure functions in `tiny-clj.gpio` have docstrings.
- Native C implementations in `src/gpio_host.c` and `src/gpio_esp32.c` have Doxygen comments.
- Error messages and native arity names were aligned with the public `tiny-clj.gpio` function names.

## Performance Constraints

### Implemented

- ISR-side enqueueing was replaced with a flag-only bridge plus thread-context draining.
- Runtime stats expose GPIO event drops.
- The ESP32 run loop uses an adaptive sleep strategy to reduce timer and GPIO latency.
- ESP32 GPIO now caches repeated digital/PWM configuration steps to avoid redundant driver calls.
- For bounded ESP32 pin domains, pin-indexed slot storage is preferred over linear persistent-map lookups on hot runtime paths.
- ESP32 now prefers one shared per-pin runtime state slot over multiple independent pin-indexed containers when the data is hot and bounded.

### Why this still matters

- GPIO callbacks are part of the game/runtime hot path.
- Event overflow must remain visible through telemetry.
- The callback-first API should remain the default for low-latency input; `gpio-channel` is a convenience layer, not the fast path.

## Completed Software Scope

### Analog input

- `read-analog` was added as the low-level analog primitive.
- The public contract is now explicit: raw 12-bit ADC values `0..4095`.
- Host-side analog simulation is available via `simulate-analog!`.
- ESP32 uses the ADC oneshot driver for direct pin reads.

### Analog change filtering

- `watch-analog` was added as a Clojure helper.
- Filtering semantics are explicit: emit only when `abs(current - last-emitted) >= :threshold`.
- Polling cadence is caller-controlled through `:period-ms`.

### Resolved evaluator issue behind analog watch

- The analog-watch callback failure was traced to evaluator handling for map/set literals passed as function arguments.
- It was not a general closure-capture bug.
- Minimal REPL repro:

```clojure
((fn [pin]
   (let [value 0]
     (identity {:pin pin :value value})))
 35)
```

- Historical failing result:
  - threw `RuntimeException: Unable to resolve symbol: pin in this context`
- Important counterexamples:
  - direct map return works: `((fn [pin] (let [value 0] {:pin pin :value value})) 35)`
  - vector argument works: `((fn [pin] (let [value 0] (identity [pin value]))) 35)`
  - simple timer/closure capture without event-map literal works
- Root cause:
  - `eval_arg_from_expr_with_context` evaluated map-literal keys and values with `eval_body(..., NULL)` instead of forwarding the active `ctx`
  - that dropped the lexical frame/environment when a map literal was evaluated in function-argument position
- Fix:
  - the map-literal path now evaluates keys and values with `eval_body(..., ctx)`
  - this preserves lexical locals for callback/event-map construction
- Why this matters for GPIO:
  - `watch-analog` invokes the user callback with an event map literal
  - that event shape matched the failing evaluator pattern, so GPIO exposed the evaluator bug instead of owning it
- Cleanup completed after the fix:
  - temporary `(def ...)`-based watch-analog test workarounds were removed
- Regression coverage now in place:
  - `src/tests/test_let.c:test_let_map_literal_argument_resolves_local_symbol`
  - `src/tests/test_let.c:test_let_map_literal_argument_preserves_param_and_inner_local`
  - `src/tests/test_gpio_write.c:test_gpio_watch_analog_signal_delivers_initial_event_with_local_atom`
  - `src/tests/test_gpio_write.c:test_gpio_watch_analog_threshold_filters_small_changes_with_local_atom`
  - current status: all targeted regressions pass again

### Builtin split

- GPIO-native symbol lookup was moved out of `src/builtins.c` into `src/builtins_gpio.c`.
- This keeps the runtime contract unchanged while reducing pressure on the central builtin table.

## API Direction

### Goal

- The public GPIO API should become more minimal, more generic, and more powerful without hiding important runtime semantics.
- The shared abstraction is "observe value changes on a pin".
- API surface should prefer one public watcher name plus an options map over multiple top-level watcher names.

### Recommendation

- Keep `watch` as the main public watcher API.
- Extend `watch` so callers can select the signal kind through options instead of function name.
- Introduce pin-mode state as runtime-owned GPIO semantics instead of relying on hardware introspection.
- Let public `pin-read` / `pin-write` dispatch depend on that runtime pin-mode state, not on ESP-IDF or host-driver introspection.
- On ESP32, store hot pin-indexed runtime state in fixed pin slots where the domain is bounded; keep map-based fallback/storage only where that remains the better fit.
- Prefer one shared ESP32 per-pin state struct for semantic mode, watcher callback, and hot cache flags instead of separate pin-keyed maps/vectors.
- Rename the public setter to `set-pin-mode!`.
- Add a public reader `pin-mode` for explicit mode introspection.
- Rename the public generic read/write operations to `pin-read` and `pin-write`.
- Rename the public PWM operation to `pin-pwm!`.
- Add readable digital level symbols `HIGH` and `LOW` for use with `pin-write`.
- Move toward mode-sensitive public `pin-read` and `pin-write` behavior implemented as native entry points:
  - `pin-read` should dispatch by configured mode (`:input` vs `:adc`)
  - `pin-write` should dispatch by configured mode (`:output` vs `:dac`)
- Do not let `pin-read` or `pin-write` guess a mode when none was configured.
- Keep low-level access paths available during migration:
  - `read-analog` remains the raw ADC primitive
  - `pin-pwm!` remains the explicit PWM primitive
- Keep separate implementation paths internally:
  - digital watch path remains interrupt/event-loop oriented
  - analog watch path remains polling-based unless real hardware analog threshold interrupts are introduced later
- Consolidate the native GPIO/LEDC control surface in `src/gpio.c`.
- Make `builtins_gpio.c` a thin argument-parsing layer above that C API.
- Make the ESP32 sound backend use the same GPIO/LEDC C core instead of configuring LEDC independently.

### Why a unified public watch API fits this codebase

- It is more minimal: users only need to remember `watch`.
- It is more generic: digital and analog observations share one entry point.
- It is still honest about semantics because mode-specific options remain explicit.
- It fits the existing layering rule: keep native primitives small and build richer behavior in Clojure.
- It avoids overloading hardware inspection with API meaning; the runtime owns the semantic mode model explicitly.

### Proposed public shape

```clojure
(pin-write pin LOW)
(pin-write pin HIGH)
(watch pin callback)
(watch pin callback {:signal :digital})
(watch pin callback {:signal :analog :period-ms 50 :threshold 4 :emit-initial? true})
(watch pin nil)
```

- Recommended event shapes:
  - digital: `{:source :gpio :signal :digital :kind :edge :pin <fixnum> :value <0|1>}`
  - analog: `{:source :gpio :signal :analog :kind :analog :pin <fixnum> :value <0..4095> :delta <fixnum>}`
- The analog polling helper now lives behind the unified public `watch` API.

### Boundaries and non-goals

- Do not force digital and analog onto one backend implementation.
- Do not hide that digital watch is low-latency and interrupt-driven while analog watch is currently timer/polling-driven.
- Do not merge `pin-read` and `read-analog`; their value domains and hardware paths are meaningfully different.
- Do not derive semantic pin modes from ESP-IDF state alone; LEDC/PWM, ADC, DAC, and plain GPIO do not share one reliable public mode query.
- Do not have the sound backend call `native_gpio_*` builtin entry points directly.
- Do not keep separate GPIO-PWM and sound-LEDC ownership logic once the shared C core exists.

## Remaining External Validation

### Board validation

- Real-device ESP32 validation is still outstanding:
  - edge callback delivery
  - PWM output start/stop
  - analog reads on board wiring
  - event-drop counters under burst input

This is the only significant remaining step outside the host-side software implementation.

## DAC Write Support

- `pin-write` now dispatches through the shared GPIO C core for both:
  - `:output` -> digital write
  - `:dac` -> analog DAC write
- Host keeps `:dac` behavior testable by storing the last analog output value in the shared runtime analog state.
- ESP32 uses the DAC oneshot driver through the backend layer and supports the DAC-capable pins on the target SoC.
- Public `:dac` values use the raw 8-bit DAC domain `0..255`.

## Migration Sketch

### Target public API

```clojure
(pin-write 2 HIGH)
(pin-write 2 LOW)
(pin-pwm! 18 1000 128)
(watch pin callback)
(watch pin callback {:signal :digital})
(watch pin callback {:signal :analog})
(watch pin callback {:signal :analog :period-ms 50 :threshold 4 :emit-initial? true})
(watch pin nil)
```

- Default mode should stay digital so the common low-latency case remains short and unsurprising.
- `callback = nil` should remain the removal form for digital watch registrations.
- Analog watch should continue to return a handle map with `:close!`, even when entered through unified `watch`.
- The same `pin-modes*` state now supports:
  - `(set-pin-mode! 35 :adc)` followed by `(pin-read 35)`
  - `(set-pin-mode! 2 :output)` followed by `(pin-write 2 HIGH)`
  - `(set-pin-mode! 25 :dac)` followed by `(pin-write 25 128)`

### Return-value contract

- Digital:
  - keep returning `nil` for `(watch pin callback)` and `(watch pin nil)`
  - this preserves the existing low-level contract and keeps `gpio-channel` unchanged
- Analog:
  - return the current analog watch handle map
  - `{:pin pin, :timer-id timer-id, :close! (fn [])}`

This means unified `watch` remains one public entry point, even though the return contract is mode-sensitive.
That is acceptable here because the lifecycle already differs between interrupt-backed digital watching and timer-backed analog watching.

### Event contract

- Keep existing event fields stable and add discriminators where they help.
- Recommended direction:
  - digital event: `{:source :gpio :signal :digital :kind :edge :pin <fixnum> :value <0|1>}`
  - analog event: `{:source :gpio :signal :analog :kind :analog :pin <fixnum> :value <0..4095> :delta <fixnum>}`

Implementation note:
- adding `:signal` is useful because it lets downstream consumers branch without overloading `:kind`
- downstream consumers can branch on `:signal` without overloading `:kind`

### Internal mode state

- Add a private atom in `tiny-clj.gpio`, conceptually:

```clojure
(def ^:private pin-modes* (atom {}))
```

- Recommended map shape:

```clojure
{5  {:mode :input}
 18 {:mode :pwm :freq 1000 :duty 128}
 25 {:mode :dac}
 35 {:mode :adc}}
```

- Responsibilities of `pin-modes*`:
  - represent semantic API intent, not just current hardware register state
  - work the same way on host and ESP32
  - let `pin-read`, `pin-write`, and `watch` dispatch consistently
  - keep PWM/DAC/ADC semantics explicit even when the underlying drivers expose only partial introspection
- Implementation direction after native consolidation:
  - host can keep flexible map-backed state
  - ESP32 should prefer one shared pin-state slot array for bounded per-pin runtime state such as semantic modes, watcher callbacks, and hot lookup/cache data
- Public access pattern:
  - write through `set-pin-mode!`
  - read through `pin-mode`

### Native C core

- Target shape:
  - `src/gpio.c` and `src/gpio.h` define the platform-neutral native GPIO API used by the rest of the runtime
  - `src/gpio_esp32.c` and `src/gpio_host.c` become backend implementations behind that API
  - `src/builtins_gpio.c` becomes a thin Clojure/VM adapter only
- Performance direction:
  - keep the public API platform-neutral
  - allow ESP32 internals to use a shared pin-state slot array instead of separate persistent maps/vectors when the state is naturally keyed by bounded GPIO pin numbers
- This C core should own:
  - digital read/write operations
  - PWM start/update/stop operations
  - resource reservation and release rules
  - any LEDC bookkeeping shared between GPIO and sound
- This C core should not depend on `ID *args` builtin calling conventions.
- The sound backend should call this C core directly, not `native_gpio_pwm` or other builtin entry points.

### Sound interaction

- The game/demo sound trigger path stays in Clojure:
  - GPIO watch callback triggers `tiny-fx.sound/play-steps!`
- The buzzer output path on ESP32 should be consolidated with GPIO PWM in C:
  - sound currently drives LEDC directly in `src/sound_backend_esp32.c`
  - GPIO PWM currently drives LEDC separately in `src/gpio_esp32.c`
- Final direction:
  - one shared LEDC ownership model
  - one shared pin/channel reservation model
  - one shared PWM start/update/stop implementation in the GPIO C core
- This avoids resource conflicts between `pin-pwm!` and the buzzer engine and removes duplicated LEDC setup logic.

### Mode semantics

- Recommended initial semantic set:
  - `:input`  -> digital readable with `pin-read`
  - `:output` -> digital writable with `pin-write`
  - `:adc`    -> analog readable with `pin-read` and low-level `read-analog`
  - `:dac`    -> analog writable with `pin-write`
  - `:pwm`    -> explicitly configured through `pin-pwm!` or `set-pin-mode!`
- If no semantic mode was configured for a pin, `pin-read` and `pin-write` should throw instead of silently defaulting to digital behavior.
- Digital `pin-write` should accept readable symbol values `HIGH` and `LOW` as the preferred API form.
- Digital writes should use `HIGH` and `LOW` as the canonical public API values.
- `:pwm` should remain separate from `pin-write` because PWM depends on timer/frequency state and is not just a scalar write.
- `nil` mode should remove the pin entry from `pin-modes*` and release mode-specific resources such as PWM bindings.

### Implementation shape in `libs/tiny-clj/gpio.clj`

- Keep the current native `watch` binding as the low-level digital primitive.
- Keep `set-pin-mode!` as the only public pin-mode setter; remove the old `gpio-mode!` compatibility wrapper.
- Provide `pin-mode` as the public reader for one pin's semantic mode entry.
- Keep `pin-pwm!` as the public PWM name and wire it directly to the native PWM builtin.
- Add public constants or vars for `HIGH` and `LOW` in `tiny-clj.gpio`.
- Rename that private implementation helper in Clojure to avoid recursive confusion, for example conceptually:
  - public `watch` = new dispatcher
  - private digital helper = current native watcher entry
  - private analog helper = current `watch-analog` body
- The public `watch` dispatcher should:
  - inspect `callback`
  - inspect `opts`
  - default `:signal` to `:digital`
  - route digital to the native watcher
  - route analog to the current polling helper
- `set-pin-mode!` should:
  - validate requested mode/options
  - perform required native side effects
  - update runtime pin-mode state used by native `pin-read` / `pin-write`
- `pin-mode` should:
  - return the configured entry for one pin, or `nil` if unset
- `pin-pwm!` should:
  - preserve the current `(pin freq-hz duty)` parameter shape from `pwm!`
- `pin-read` and `pin-write` should no longer be implemented as Clojure-level dispatchers.
- They should resolve directly to native builtins that:
  - read runtime pin-mode state
  - dispatch to digital, ADC, and later DAC paths in `src/gpio.c`
  - throw a clear error when no mode entry exists instead of guessing
- `pin-write` should accept `HIGH` / `LOW` for digital output as the public API form, with normalization handled in the native path if needed.

### Native implementation shape

- `src/gpio.c` should expose a small runtime-facing API, conceptually:
  - get/set/clear semantic pin-mode state
  - native mode-sensitive `pin-read`
  - native mode-sensitive `pin-write`
  - read digital pin
  - write digital pin
  - read analog pin
  - start/update PWM on a pin
  - stop PWM on a pin
  - reserve/release PWM-capable resources when needed
- `src/builtins_gpio.c` should only:
  - validate arity and argument types
  - convert Clojure values to native values
  - call the shared GPIO C API
  - raise exceptions on API errors
- `src/builtins_gpio.c` should register `pin-read` and `pin-write` as their own native entry points rather than routing through Clojure helper functions.
- `src/sound_backend_esp32.c` should:
  - stop owning its own LEDC setup path
  - call the shared GPIO C API for buzzer voice output
  - rely on the shared GPIO core for LEDC reservation and release
- `src/gpio_esp32.c` should remain the ESP32 backend implementation, but the runtime-facing API surface should be centralized in `src/gpio.c`.

### Current native split to remove

- Current state in sources:
  - `src/gpio_esp32.c` already owns GPIO watch delivery, ADC access, and its own static LEDC PWM binding table
  - `src/sound_backend_esp32.c` owns a second LEDC mapping for buzzer voices
  - `src/builtins_gpio.c` is already a clean native lookup layer and should stay thin
  - `src/gpio_common.h` currently holds shared argument parsing helpers
- Main problem:
  - LEDC ownership is split across two ESP32 implementation files
  - pin/channel/timer reservation is not centralized
  - long-term behavior can drift between `pin-pwm!` and sound playback

### Target file layout

- Add new runtime-facing files:
  - `src/gpio.c`
  - `src/gpio.h`
- Keep backend files:
  - `src/gpio_esp32.c`
  - `src/gpio_esp32.h`
  - `src/gpio_host.c`
  - `src/gpio_host.h`
- Responsibility split:
  - `src/gpio.h`
    - declares the stable runtime-facing GPIO API
    - does not expose builtin calling conventions
  - `src/gpio.c`
    - dispatches to host/ESP32 backend functions
    - centralizes cross-runtime policy, semantic pin-mode dispatch, and ownership boundaries
    - owns shared runtime state that should not live in host-specific backends
  - `src/gpio_esp32.c`
    - implements actual ESP-IDF GPIO/ADC/LEDC calls
    - owns ESP32-specific state hidden behind backend entry points
  - `src/gpio_host.c`
    - implements host test/simulation semantics behind the same backend entry points
    - should stay stateless or near-stateless apart from strictly host-specific behavior
  - `src/gpio_common.h`
    - may keep small shared parsing/range helpers
    - should not become the long-term home of runtime GPIO behavior

### Proposed native C API shape

- Prefer a direct C API over builtin-shaped functions, conceptually:
  - `gpio_watch_set(pin, callback)` / `gpio_watch_clear(pin)`
  - `gpio_pin_mode_set(pin, mode, opts)` / `gpio_pin_mode_get(pin)` / `gpio_pin_mode_clear(pin)`
  - `gpio_pin_read(pin)`
  - `gpio_pin_write(pin, value)`
  - `gpio_write_digital(pin, level)`
  - `gpio_read_digital(pin, &level)`
  - `gpio_read_analog(pin, &value)`
  - `gpio_pwm_start_or_update(pin, freq_hz, duty)`
  - `gpio_pwm_stop(pin)`
- Optional extension for sound integration if needed:
  - a reservation-oriented API such as `gpio_pwm_reserve_pin(...)` / `gpio_pwm_release_pin(...)`
  - or a channel-based API if the sound backend truly needs fixed voice-channel mapping
- Rule:
  - expose the smallest API that both GPIO builtins and sound backend can use without duplicate LEDC logic
  - do not add thin forwarding wrappers that only rename another public C function

### Migration phases

1. Create `src/gpio.h` and `src/gpio.c` with the public runtime-facing C API.
2. Move builtin implementations to call that API instead of owning GPIO behavior directly.
3. Keep watch and ADC behavior where it is functionally stable; only reroute call paths first.
4. Move semantic pin-mode state plus `pin-read` / `pin-write` dispatch into `src/gpio.c` as native runtime behavior.
5. Extract PWM/LEDC ownership from `src/gpio_esp32.c` into backend-facing helpers reachable through `src/gpio.c`.
6. Refactor `src/sound_backend_esp32.c` to use the shared PWM/LEDC API.
7. Remove duplicated LEDC mapping/initialization state from the sound backend.
8. Add regression tests for shared reservation/conflict behavior before cleanup of old paths.

Progress so far:
- Step 1 is implemented.
- Step 2 is implemented.
- A direct shared runtime C API in `src/gpio.h` / `src/gpio.c` is implemented and used by non-builtin runtime callers.
- Step 6 is implemented:
  - sound now uses the shared GPIO PWM backend
  - event loop and runtime stats now also use the shared GPIO layer instead of ESP32 GPIO backend headers directly
- Step 7 is implemented:
  - duplicated sound-side LEDC setup and per-voice ownership state were removed
- Step 8 is implemented at the architecture-contract level:
  - regression tests assert that GPIO builtins and the sound backend use the shared core layering
  - regression tests assert that the sound backend no longer keeps separate LEDC setup/ownership logic
- New target direction:
  - `pin-read` and `pin-write` should become direct native builtins
  - semantic pin-mode state and dispatch should move into `src/gpio.c`
  - the temporary Clojure-level dispatcher approach should be retired once the native path lands

### Risk notes for consolidation

- Watch delivery and PWM consolidation have different risk profiles:
  - watch is event-loop/ISR sensitive
  - PWM is resource-ownership sensitive
- Recommended order:
  - consolidate PWM/LEDC ownership first
  - keep digital watch internals mostly untouched during that step
- Reason:
  - the sound interaction problem is specifically about duplicated LEDC ownership
  - watcher behavior is already functionally separate and not part of the buzzer conflict

### Migration cutover

1. Keep `gpio-channel` calling `watch` without opts so its behavior stays digital and unchanged.
2. Update examples, smoke scripts, and docs so only unified `watch` is documented.
3. Keep analog close semantics explicit via the returned handle.

### Test migration plan

- Add focused tests for `pin-modes*` state transitions:
  - `set-pin-mode!` stores mode metadata
  - `(set-pin-mode! pin nil)` removes stored mode metadata
  - `pin-mode` returns the stored entry for a configured pin
- Add focused constant/API tests for:
  - `HIGH` maps to digital high
  - `LOW` maps to digital low
  - `pin-pwm!` preserves the current PWM argument contract
- Add focused native-integration tests for the C consolidation:
  - GPIO builtins and sound backend do not maintain separate LEDC reservation state
  - PWM resource conflicts are detected consistently through the shared GPIO C API
- Preserve the existing digital tests for:
  - host `watch` event delivery
  - `(watch pin nil)` removal
- Retarget the current analog tests so they also cover:
  - `(watch pin callback {:signal :analog ...})`
  - removal of `watch-analog` from the final public API
  - event-map shape stability
- Add one focused contract test for default dispatch:
  - `(watch pin callback)` must still behave as digital watch
- Add later dispatcher tests for:
  - `(set-pin-mode! pin :adc)` + `(pin-read pin)`
  - `(set-pin-mode! pin :dac)` + `(pin-write pin value)`
  - `(set-pin-mode! pin :output)` + `(pin-write pin HIGH)`
  - `(set-pin-mode! pin :output)` + `(pin-write pin LOW)`
  - `(pin-read pin)` without prior `set-pin-mode!` throws
  - `(pin-write pin value)` without prior `set-pin-mode!` throws

### Open design choice

- There are two viable contracts for `(watch pin nil)` when `:signal :analog` is involved:
  - unsupported, because analog watch owns a timer handle and should be closed through `:close!`
  - supported only when the caller passes the handle or a stable watch key

Recommendation:
- do not overload `(watch pin nil)` for analog mode in the first migration
- keep analog close semantics explicit through the returned handle
- this keeps the minimal API surface while avoiding hidden timer-identity rules

### Scope boundary

- Do not move analog watching into C at this stage.
- Do not change `pin-read`, `read-analog`, `simulate!`, or `simulate-analog!` as part of the public watch unification.
- Do not redesign `gpio-channel` until unified `watch` is in place and proven stable.
- Do not use hardware introspection as the primary source of truth for pin semantics; `pin-modes*` owns that role.
- Do not leave long-term LEDC duplication between `src/gpio_esp32.c` and `src/sound_backend_esp32.c`.

## Example

```clojure
(require 'tiny-clj.gpio)

(defonce led-state (atom 0))

(defn toggle-led! [pin]
  (let [next (if (zero? @led-state) 1 0)]
    (reset! led-state next)
    (tiny-clj.gpio/pin-write pin (if (zero? next) tiny-clj.gpio/LOW tiny-clj.gpio/HIGH))))

(schedule-periodic
  0
  500
  {:id :status-led
   :fn (fn [] (toggle-led! 2))})
```

```clojure
(require 'tiny-clj.gpio)

(tiny-clj.gpio/set-pin-mode! 35 :adc)
(tiny-clj.gpio/pin-read 35)

(def adc-watch
  (tiny-clj.gpio/watch
    35
    (fn [{:keys [value delta]}]
      (println "adc" value "delta" delta))
    {:signal :analog
     :period-ms 50
     :threshold 8}))
```

## Verification Snapshot

- Verified host GPIO test set: 32 tests passing.
- Verified integration checks:
  - `test_vector_scene_graph_game_demo_gpio_press_triggers_demo_melody_once`
  - `test_plan_trackA_gpio_smoke_script`
- Verified embedded-source coverage:
  - `test_embedded_sources_tiny_clj_gpio`
- Verified architecture-contract coverage for the consolidation:
  - shared GPIO builtins now live in `src/gpio.c`
  - runtime callers consume `src/gpio.h`
  - ESP32 sound output uses the shared GPIO PWM path
- Verified targeted host regression checks after the final cleanup:
  - `./build/unit-tests -test test_thread_sleep_namespace_advances_time_and_returns_nil`
  - `./build/unit-tests -test test_sound_native_host_status_returns_map`
  - `./build/tiny-clj-repl -e "(do (Thread/sleep 10) :ok)"`
  - `./build/tiny-clj-repl -e "(do (require 'tiny-fx.sound-demos) (let [ret (tiny-fx.sound-demos/play-the-entertainer!)] (Thread/sleep (:duration-ms ret)) ret))"`
- Verified new evaluator/REPL reproduction steps for the analog-watch investigation:
  - REPL repro for direct timer/closure capture still works:
    - `(do (let [x 42 out (atom nil) timer-id (schedule-periodic 0 1 (fn [] (reset! out x) nil))] (run-next-task) (cancel-timer timer-id) @out))`
  - REPL repro for map-literal argument evaluation now succeeds:
    - `(let [pin 35] (identity {:pin pin}))`
  - mixed param + inner-`let` map-literal argument also succeeds:
    - `((fn [pin] (let [value 0] (identity {:pin pin :value value}))) 35)`
  - analog watch with local lexical callback state now succeeds again:
    - `(do (require 'tiny-clj.gpio) (let [events (atom []) w (tiny-clj.gpio/watch 35 (fn [ev] (swap! events conj ev) nil) {:signal :analog :period-ms 1 :threshold 0 :emit-initial? true})] (run-next-task) ((get w :close!)) @events))`
  - targeted evaluator and analog-watch regressions now pass:
    - `cmake --build build --target unit-tests -j4`
    - `./build/unit-tests -test test_let_map_literal_argument_resolves_local_symbol`
    - `./build/unit-tests -test test_let_map_literal_argument_preserves_param_and_inner_local`
    - `./build/unit-tests -test test_gpio_watch_analog_signal_returns_handle_and_close`
    - `./build/unit-tests -test test_gpio_watch_analog_signal_delivers_initial_event_with_local_atom`
    - `./build/unit-tests -test test_gpio_watch_analog_threshold_filters_small_changes_with_local_atom`
- Plan closure summary:
  - the GPIO architecture and consolidation work is complete
  - unified analog/digital `watch` is implemented and its analog callback delivery works again with lexical locals
  - real-device ESP32 board validation remains open
  - mode-sensitive `pin-write` via `:dac` is implemented in the shared GPIO core
  - remaining follow-up here is limited to real-device ESP32 validation, not a known software blocker in the host/runtime path

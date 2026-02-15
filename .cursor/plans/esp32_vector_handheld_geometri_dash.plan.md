---
name: ""
overview: ""
todos: []
isProject: false
---

# ESP32 Vector Handheld Plan (tiny-clj + C)

## Goal

Build a compact ESP32 handheld game ("Geometri Dash" style) with:

- ST7789 320x240 display over SPI
- Single-threaded runtime
- Game logic in tiny-clj (interpreted)
- Rendering and audio in C
- Configurable-channel retro sound on passive piezos via LEDC PWM (board default: 2 outputs)

## Constraints

- MCU: ESP32 (WEMOS Lite V1 class), 4 MB flash
- No MP3, no I2S audio stack, no heavy media libs
- Stable frame pacing preferred over max visual complexity
- tiny-clj should not do per-pixel work

## Definition Of Done (project-level)

1. Device boots directly into game loop.
2. Input latency feels immediate (< 1 frame typical).
3. Stable 60 FPS target, fallback 30 FPS if board/load requires.
4. Continuous music (default 2 voices, skalierbar auf N Kanaele) + concurrent SFX without audible glitches.
5. Clean reset/restart cycle after collision without memory growth.

## Milestone 0: Hardware Lock-In

Status: TODO

Tasks:

- Finalize GPIO map for:
  - ST7789 (SCLK, MOSI, CS, DC, RST, BL)
  - Encoder (A/B/SW)
  - D-pad (4 buttons) — optional, may come later
  - Optional A/B action buttons
  - Piezo outputs (board default: Piezo 1 + Piezo 2)
  - Battery ADC pin + divider values
- Validate boot-safe pins (avoid strapping conflicts).
- Commit board profile header with final values.

Done when:

- Pin map compiles and device boots reliably across power cycles.

## Milestone 1: Display Bring-Up (C)

Status: TODO

Tasks:

- Add ST7789 init + orientation + RGB565 clear.
- Implement primitives:
  - clear(color)
  - hline/vline
  - rect fill
- Measure full-frame redraw time at target SPI clock.
- Add optional dirty-rect mode switch.

Done when:

- A fixed test scene renders at target FPS with no tearing artifacts.

## Milestone 2: Input Layer (C)

Status: TODO

Tasks:

- Configure button GPIOs (pullups, debounce strategy).
- Implement encoder decode with edge-safe state machine.
- Build frame input snapshot struct:
  - held bits
  - pressed bits (edge)
  - encoder delta
- Expose one polling function called once per frame.

Done when:

- Input test page shows correct edge/held states with no bounce spam.

## Milestone 3: Audio Core (C, LEDC)

Status: TODO

Tasks:

- 2 independent square-wave voices on LEDC channels.
- 1 ms scheduler tick (esp_timer).
- Command queue:
  - note on/off
  - frequency set
  - volume/envelope step
  - priority class
- Ducking policy (music lowers under important SFX).
- Implement SFX presets:
  - laser
  - explosion
  - hit
  - menu
  - r2d2-style chirp

Done when:

- Music + overlapping SFX play cleanly with no frame hitching.

## Game model: Clojure data structures (recommendations)

For the tiny-clj game loop, keep state flat and allocation-bounded:

- **Maps** for game state: one main state map with a small, fixed set of keys (e.g. `:player-x`, `:player-y`, `:vy`, `:score`, `:phase`, `:seed`). Use `assoc`/`update` for single-key changes to avoid full copy cost where possible; tiny-clj’s persistent semantics still apply.
- **Vectors** for ordered, bounded sequences:
  - Obstacles in scroll order (index = draw order; fixed max length, reuse slots when off-screen).
  - Render command list and audio event list (already in M4 contract); cap length per frame.
- **Keywords** for enums: `:playing`, `:dead`, `:menu`, obstacle types (e.g. `:spike`, `:block`).
- **Avoid**: large lazy seqs, deep nesting, per-frame brand-new big maps. Prefer a single “world” map plus small vectors that are truncated or rotated instead of rebuilt.

Input from C can be a small map (e.g. `{:held bits :pressed bits :encoder-delta n}`) so Clojure code stays keyword-based and readable.

- **No atoms** for game state: the runtime is single-threaded and the frame contract is “state in → new state out”. The C host holds the current state and passes it in each frame; Clojure’s `update` returns the new state. Using an atom (`swap!` each frame) would not reduce allocation (the new map is still created every frame) and would add mutable state inside the interpreter without benefit. Keeping state at the boundary (C owns the reference) keeps the loop pure, testable, and replay-friendly.

## Milestone 4: tiny-clj Host Contract

Status: TODO

Tasks:

- Define strict frame contract:
  - Input map in
  - New state out
  - Render command vector out
  - Audio event vector out
- Add low-allocation native bridge helpers.
- Keep command vocabulary small:
  - :clear
  - :line
  - :rect
  - :tone / :sfx
- Add guardrails:
  - max render commands per frame
  - max audio events per frame

Done when:

- tiny-clj update function can drive one frame end-to-end from C host.

## Milestone 5: Core Gameplay (tiny-clj)

Status: TODO

Tasks:

- Implement pure update(state, input, dt-ms):
  - jump physics
  - obstacle scroll/spawn
  - collision
  - score/progress
- Emit render/audio intents only, no direct hardware calls.
- Add deterministic seed/path mode for repeatable tests.

Done when:

- Loop is playable, restart works, score increases correctly.

## Milestone 6: Performance + Stability

Status: TODO

Tasks:

- Profile frame time buckets:
  - input
  - tiny-clj update
  - render submit
  - flush
  - audio tick load
- Reduce tiny-clj allocations in hot path.
- Add watchdog-safe frame budget behavior:
  - skip optional effects under overload
  - preserve gameplay timing first
- Run 30+ min soak test.

Done when:

- No crash, no progressive slowdown, no visible GC spikes during normal play.

## Milestone 7: Power + Battery UX

Status: TODO

Tasks:

- ADC calibration and moving-average battery reading.
- Low-battery thresholds:
  - warning overlay
  - optional backlight dim
- Idle timeout for menu screen.

Done when:

- Battery reading is stable enough for user-facing indicator.

## Milestone 8: Mechanical Integration Checks

Status: TODO

Tasks:

- Confirm display window alignment and bezel margins.
- Validate piezo cavity gap (1-2 mm) and mounting tape method.
- Check button and encoder feel through front plate.
- Verify USB cable clearance and screw stack-up.

Done when:

- Enclosure assembly passes repeated open/close cycles without stress issues.

## Recommended Implementation Order (first 2 weeks)

1. M0 hardware lock-in
2. M1 display bring-up
3. M2 input layer
4. M3 audio core skeleton
5. M4 tiny-clj host contract stub
6. M5 minimal playable loop

## Risk Register

- Risk: SPI redraw too slow at full-screen every frame.
  - Mitigation: dirty-rect mode + command capping.
- Risk: Audio jitter from timer contention.
  - Mitigation: very small ISR work, queue events, keep synthesis simple.
- Risk: tiny-clj allocation spikes.
  - Mitigation: flat state model, bounded vectors, reuse patterns.
- Risk: Boot pin conflicts from chosen GPIOs.
  - Mitigation: lock final pin map before PCB/perfboard wiring freeze.

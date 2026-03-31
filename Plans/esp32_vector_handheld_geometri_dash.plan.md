---
name: "ESP32 Vector Handheld Geometri Dash"
overview: "Repo-basierter Umsetzungsplan fuer ESP32-Handheld mit tiny-clj Gameplay, C-Render und C-Sound."
todos:
  - "ST7789 ESP32 Backend an Renderer-Lifecycle anbinden"
  - "Input Snapshot Layer fuer Encoder + Buttons bauen"
  - "Gameplay Frame-Contract (state/input/patches/audio) finalisieren"
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

## Repo Snapshot (2026-03-16)

Current state derived from code and tests in this repository:

1. Graphics core in C is far advanced and tested.
- Evidence:
  - `/Users/theisen/Projects/tiny-clj/src/vector_scene_graph.c`
  - `/Users/theisen/Projects/tiny-clj/src/scene.c`
  - `/Users/theisen/Projects/tiny-clj/src/render_backend.c`
  - `/Users/theisen/Projects/tiny-clj/src/builtins_tiny_fx_gfx.c`
  - `/Users/theisen/Projects/tiny-clj/libs/tiny-fx/gfx.clj`
- Validation:
  - `./build/unit-tests --test 'test_vector_scene_graph/*'` -> `99 Tests, 0 Failures`.

2. Sound core in C + tiny-clj API is far advanced and tested.
- Evidence:
  - `/Users/theisen/Projects/tiny-clj/src/sound_engine.c`
  - `/Users/theisen/Projects/tiny-clj/src/sound_backend_esp32.c`
  - `/Users/theisen/Projects/tiny-clj/src/builtins_sound.c`
  - `/Users/theisen/Projects/tiny-clj/libs/tiny-fx/sound.clj`
- Validation:
  - `./build/unit-tests --test 'test_sound_engine/*'` -> `104 Tests, 0 Failures`.

3. Board profile exists, but appears to be template/default and not final board wiring.
- Evidence:
  - `/Users/theisen/Projects/tiny-clj/esp32-idf/main/vector_handheld_config.h`

4. Missing for device bring-up:
- No ESP32 ST7789 backend wiring into `tiny_renderer_lifecycle_set_callbacks`.
- No project-specific input snapshot layer for encoder + D-pad + action buttons.
- No geometri-dash gameplay loop wired to frame contract on-device.

5. Assumed completed dependency:
- `Plans/edn-assets-on-demand-test-first-plan.md` is treated as implemented.
- Consequence: EDN asset loading is not a blocker for gameplay bring-up in this plan.

## Upstream Dependency: Vector Scene Graph Engine

Primary dependency plan:
- `/Users/theisen/Projects/tiny-clj/Plans/esp32_vector_scene_graph_engine.plan.md`

Integration rule:
- This Geometri Dash plan consumes the scene-graph renderer contract and patch path from the dependency plan, instead of defining a separate render primitive stack.

Hard gates:
- Gate A (before gameplay rendering integration):
  - Scene-graph plan M0..M5 must pass (record schema, macOS simulator PoC, transforms, primitives, thick lines, id-based patches).
- Gate B (before full on-device gameplay rendering):
  - Scene-graph plan M7 must pass (ESP32 SPI backend integration on shared render core).

## Definition Of Done (project-level)

1. Device boots directly into game loop.
2. Input latency feels immediate (< 1 frame typical).
3. Stable 60 FPS target, fallback 30 FPS if board/load requires.
4. Continuous music (default 2 voices, skalierbar auf N Kanaele) + concurrent SFX without audible glitches.
5. Clean reset/restart cycle after collision without memory growth.
6. Gameplay render path uses the shared scene-graph engine contract (no parallel custom renderer).

## Milestone 0: Hardware Lock-In
Status: IN_PROGRESS

Tasks:
- Finalize GPIO map for:
  - ST7789 (SCLK, MOSI, CS, DC, RST, BL)
  - Encoder (A/B/SW), D-pad, optional A/B action
  - Optional A/B action buttons
  - Piezo outputs (board default: Piezo 1 + Piezo 2)
  - Battery ADC pin + divider values
- Validate boot-safe pins (avoid strapping conflicts).
- Replace template defaults in `vector_handheld_config.h` with verified wiring.

Done when:
- Pin map compiles and device boots reliably across power cycles.

## Milestone 1: Graphics Pipeline
Status: PARTIAL

Tasks:
1. Done:
- Scene graph rendering core, clipping, dirty-region flow, runtime query builtins.
2. Open:
- Add ESP32 ST7789 backend (`begin_frame/submit_rect/end_frame`) and register it via lifecycle callbacks.
- Wire backend start/stop into ESP32 runtime startup path.
- Validate panel orientation, backlight, and throughput.

Done when:
- C scene graph output is visible on real ST7789 display through lifecycle backend path.

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
Status: PARTIAL

Tasks:
1. Done:
- 2-voice ESP32 backend via LEDC/PWM and `esp_timer` tick scheduling.
- Sound engine queueing, track loading, music + SFX APIs, envelope support.
2. Open:
- Explicit priority/ducking policy for gameplay SFX over music.
- Final on-device loudness balancing and piezo tuning.
- Final game SFX preset pack (laser/explosion/hit/menu/r2d2) as project assets.

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
Status: IN_PROGRESS

Tasks:
- Define strict frame contract (gameplay side):
  - Input map in
  - New state out
  - Scene patch vector out (id-based updates, aligned with scene-graph plan M5)
  - Audio event vector out
- Add low-allocation native bridge helpers.
- Keep render payload aligned with upstream scene schema and canonicalization path.
- Keep audio command vocabulary small:
  - :tone / :sfx
- Add guardrails:
  - max scene patches per frame
  - max audio events per frame

Done when:
- tiny-clj update function can drive one frame end-to-end using scene patches + audio events.

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
- Consume on-demand EDN asset loading (already implemented) for level/music tables; do not re-implement asset IO in gameplay loop.

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

## Next Execution Order (updated)

1. M0 hardware lock-in
2. M1 open items: ESP32 ST7789 backend + lifecycle callback wiring
3. M2 input snapshot layer (encoder/buttons)
4. M4 finalize gameplay frame contract with hard caps
5. M5 minimal playable geometri-dash loop on device
6. M3 open audio items: ducking + final SFX pack + piezo tuning
7. M6 profiling and soak
8. M7/M8 battery + mechanical finalization

## Risk Register

- Risk: SPI redraw too slow at full-screen every frame.
  - Mitigation: use upstream scene-graph clipping/patch path; avoid full-frame redraw requirement.
- Risk: Audio jitter from timer contention.
  - Mitigation: very small ISR work, queue events, keep synthesis simple.
- Risk: tiny-clj allocation spikes.
  - Mitigation: flat state model, bounded vectors, reuse patterns.
- Risk: Boot pin conflicts from chosen GPIOs.
  - Mitigation: lock final pin map before PCB/perfboard wiring freeze.
- Risk: Host simulator and ESP32 render behavior diverge.
  - Mitigation: treat scene-graph conformance gates (A/B) as release blockers for gameplay integration.

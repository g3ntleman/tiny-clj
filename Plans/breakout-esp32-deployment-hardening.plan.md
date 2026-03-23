---
name: Breakout ESP32 Deployment Hardening
overview: Prepare the Breakout runtime and deployment path for ESP32 shipment by prioritizing thread-safety of subjective-memory usage, removing hot-path allocations, reducing binary size, improving readability/documentation, and separating host-only diagnostics from shipping code.
todos:
  - id: baseline-review
    content: Review the current Breakout runtime, viewer, collision, and sound paths to capture concrete ESP32 deployment risks and define the execution order
    status: completed
  - id: remove-shipping-debug-noise
    content: Remove or isolate non-shipping debug instrumentation, absolute-path logging, and host-only diagnostics from sound and Breakout-adjacent runtime code
    status: pending
  - id: thread-ownership-contract
    content: Define and enforce which threads may touch subjective-memory objects, retain/release APIs, eval APIs, and event payload construction
    status: pending
  - id: render-thread-detox
    content: Eliminate subjective-memory ownership churn from the render thread by switching to a plain-C snapshot handoff instead of deref/retain/release of Clj objects in that thread
    status: pending
  - id: audio-callback-detox
    content: Eliminate Clj allocation and callback/event construction from sound tick threads and ESP32 timer callbacks; forward only POD commands/events into a scheduler-owned drain path
    status: pending
  - id: breakout-runtime-hotpath
    content: Remove avoidable runtime.clj/runtime-play.clj hot-path allocations and repeated require/eval lookups from input, publish-state, and collision handling
    status: pending
  - id: collision-event-shape
    content: Reduce collision dispatch allocation cost and simplify the event bridge by keeping raw hit data compact until the interpreter thread materializes higher-level objects
    status: pending
  - id: scene-build-budget
    content: Reduce scene rebuild cost by separating static scene topology from per-frame state updates and by avoiding full FrameScene reconstruction where only dynamic fields changed
    status: pending
  - id: code-size-pass
    content: Shrink code size by deduplicating Breakout runtime helpers, splitting host-only viewer code, and compiling debug-only helpers behind explicit feature gates
    status: pending
  - id: docs-and-ownership-comments
    content: Add focused documentation for thread/ownership contracts and for the runtime, collision, and sound execution model
    status: pending
  - id: regression-and-budgets
    content: Add targeted regression tests and measurement steps for heap, thread-safety, sound callbacks, collision dispatch, and ESP32 binary size before implementation sign-off
    status: pending
  - id: cleanup
    content: Sourcecode aufräumen – Debug-Code, temporäre Workarounds, tote Codepfade, überflüssige Kommentare und nicht mehr benötigte Hilfsfunktionen entfernen
    status: pending
isProject: true
---

# Breakout ESP32 Deployment Hardening Plan

## Goal

Make the Breakout deployment path safe and maintainable for ESP32 shipment:

- no subjective-memory surprises on non-interpreter threads
- no avoidable heap allocation in frame, collision, input, or audio hot paths
- smaller shipping binary
- clearer module boundaries and execution flow
- documented contracts that are reviewable before implementation starts

## Current risk snapshot

The current codebase already contains strong regression coverage around startup, heap budgets, runloop behavior, collision dispatch, and render-loop integration. That is a good base. The largest remaining deployment risks are structural:

1. The sound tick path still crosses into Clj-object ownership and event creation from non-interpreter callback contexts.
2. The render thread still dereferences and retains/releases Clj scene objects directly.
3. The Breakout runtime still performs repeated dynamic work in hot paths (`require`, `eval`, scene rebuilding, timer-spec construction).
4. Host-only debug helpers and one-off instrumentation have leaked into shipping-adjacent files, increasing risk, noise, and code size.
5. Breakout runtime logic is split across two highly similar namespaces, which increases maintenance cost and obscures the actual hot path.

## Files that matter most

Shipping-critical or shared-path files:

- `src/sound_engine.c`
- `src/sound_backend_esp32.c`
- `src/builtins_sound.c`
- `src/atom.c`
- `libs/tiny-breakout/runtime.clj`
- `libs/tiny-breakout/runtime-play.clj`
- `src/viewer_collision_dispatch.c`
- `src/viewer_collision_scene_bridge.c`

Deployment-adjacent host files that should still be cleaned up because they shape architecture, tests, and regressions:

- `src/game_demo_minifb.c`
- `src/viewer_host_runloop.c`
- `src/viewer_host_slots.c`
- `src/tests/test_breakout_runtime_startup.c`

## Priority order

Implementation should follow this order:

1. Remove non-shipping debug noise.
2. Freeze thread/ownership contracts.
3. Remove Clj ownership from render/audio callback threads.
4. Attack runtime and collision hot-path allocations.
5. Reduce scene rebuild cost.
6. Deduplicate and document the runtime layout.
7. Finish with regression, heap, and size measurement gates.

## Workstream 1: Remove non-shipping debug noise first

### Why this is first

The current sound files contain ad-hoc file logging and hypothesis-specific instrumentation. That is a direct deployment blocker for ESP32 fitness and also pollutes any review of hot-path behavior.

### Concrete measures

1. Remove the absolute-path debug log writers from:
   - `src/sound_engine.c`
   - `src/sound_backend_host.c`
   - `src/builtins_sound.c`

2. Move any debug-only host audio helpers behind a clearly named compile-time gate that is disabled for shipping builds.

3. Ensure that real-time or timer callbacks never perform:
   - `fopen`
   - `fprintf`
   - `snprintf` for diagnostics
   - environment-driven debug branching unless the branch is compiled out in shipping builds

4. Keep only telemetry that is:
   - plain integer counters
   - allocation-free
   - readable from a safe control thread

### Review gate

- No absolute-path logging remains in shipping-adjacent code.
- No I/O remains in audio tick or render callbacks.
- Debug helpers are either removed or explicitly feature-gated.

## Workstream 2: Define the thread/ownership contract

### Why this is critical

ESP32 deployment becomes fragile if subjective-memory APIs are touched from arbitrary threads. Right now the code strongly suggests that this boundary is not clean.

### Concrete measures

1. Write a short ownership/thread contract for these thread classes:
   - main thread
   - interpreter/runloop thread
   - render thread
   - sound tick thread / ESP32 timer callback
   - host UI thread

2. Explicitly define which APIs are allowed on each thread:
   - `RETAIN` / `RELEASE`
   - `AUTORELEASE`
   - `make_*`
   - `eval_*`
   - `event_loop_enqueue_ingress_call`
   - atom deref/reset APIs

3. Decide one of these two policies and document it:
   - strict policy: only interpreter/main threads may touch Clj heap objects
   - restricted policy: selected non-interpreter threads may only use a tiny audited subset

4. Add assertions or documentation markers at the entry points of:
   - `viewer_runloop_thread_main`
   - `viewer_render_thread_main`
   - `sound_timer_callback`
   - host sound tick thread entry

### Review gate

- A reviewer can point at each thread and say whether it may allocate, retain/release, or call into the interpreter.
- No ambiguous “probably safe” ownership remains.

## Workstream 3: Remove subjective-memory work from the render thread

### Problem

`src/game_demo_minifb.c` currently calls `atom_deref_owned` on the render thread and then retains/releases scene objects there. Even if this happens to work on host builds, it is exactly the kind of coupling that makes ESP32 deployment risky.

### Concrete measures

1. Replace render-thread `CljAtom` deref with a plain-C snapshot handoff prepared by the interpreter/runloop side.

2. Candidate direction:
   - runloop thread publishes a render-ready POD snapshot or retained framebuffer-ready scene buffer
   - render thread only consumes plain C data or immutable prebuilt render structures

3. Ensure render thread responsibilities are limited to:
   - waiting for slot generation changes
   - rendering from plain C snapshot state
   - transfer planning / panel submission
   - perf counters

4. Remove direct Clj-object retention/release from render-thread hot loops.

### Review gate

- The render thread does not call `atom_deref_owned`, `RETAIN`, `RELEASE`, or Clojure eval APIs.
- Render-thread state can be reasoned about without knowing subjective-memory internals.

## Workstream 4: Remove subjective-memory work from audio callback paths

### Problem

`src/sound_engine.c` currently allows finished-event creation from tick processing, and that code performs Clj allocations and ingress enqueue work. On ESP32 this ultimately runs beneath the sound timer path and violates the desired callback rules.

### Concrete measures

1. Split audio processing into two layers:
   - real-time/timer-safe engine advancement
   - scheduler/interpreter-thread notification draining

2. Replace `notify_finished()` payload construction with a POD completion queue entry such as:
   - track id
   - event kind enum
   - optional counter or timestamp

3. Drain that queue from a safe thread that is already allowed to allocate Clj maps and call `event_loop_enqueue_ingress_call`.

4. Audit all sound tick code for:
   - `RETAIN` / `RELEASE`
   - `make_map_*`
   - callback invocation
   - any implicit Clj object creation

5. Keep the ESP32 `esp_timer` callback strictly limited to:
   - reading atomics / queue state
   - advancing plain-C engine state
   - scheduling the next deadline
   - incrementing telemetry counters

### Review gate

- ESP32 sound timer callbacks perform no Clj allocation and no Clj ownership transitions.
- Finished events are still delivered, but only via a safe deferred drain path.

## Workstream 5: Remove Breakout runtime hot-path allocation

### Problem

The current Breakout runtime performs repeated dynamic work in paths that are hit during input, collision, and state publication:

- repeated `require`/`eval` lookups
- repeated map literals for timers
- repeated full scene record construction
- repeated collision-rule expansion

### Concrete measures

1. In `libs/tiny-breakout/runtime.clj` and `libs/tiny-breakout/runtime-play.clj`, replace hot-path `require`/`eval` calls with bootstrap-time resolved vars or cached function references.

2. Predeclare reusable timer specs the same way `timeline-kick-timer-spec` is already predeclared, instead of allocating fresh timer maps on every publish.

3. Consolidate duplicated helpers between the two runtime namespaces so hot-path behavior exists in one place only.

4. Audit `publish-state!`, `apply-input!`, and `on-spatial-event!` for:
   - avoidable `assoc` churn
   - repeated collision-rule expansion
   - repeated record materialization that could be deferred or reused

5. Introduce a clear distinction between:
   - state transition path
   - scene materialization path
   - event/audio emission path

### Review gate

- The hot path no longer performs dynamic namespace resolution.
- Timer scheduling in the hot path uses stable reusable specs.
- Duplicate runtime logic is reduced enough that one reviewer can trace input-to-scene flow without bouncing between two near-copy namespaces.

## Workstream 6: Reduce collision dispatch allocation cost

### Problem

Collision detection already avoids allocation during the detect step, which is good. The expensive part still happens when raw hits are drained into full `SpatialEvent` records with nested AABB records.

### Concrete measures

1. Keep the current raw-hit queue, but defer full object materialization until the interpreter thread definitely needs it.

2. Re-evaluate whether the full event shape needs:
   - two AABB records
   - both entity records
   - full rule record
   on every event.

3. Consider a lighter internal event representation first, with richer object expansion only at the public API boundary.

4. Audit whether `viewer_collision_make_spatial_event()` is on the frequency path for paddle/brick collisions during active play and measure its heap cost explicitly.

5. Keep dirty-rect filtering and latch logic, because those are already good allocation-avoidance mechanisms.

### Review gate

- Collision dispatch still preserves behavior, but the bridge no longer constructs more object graph than necessary per hit.

## Workstream 7: Reduce scene rebuild cost

### Problem

`publish-state!` currently rebuilds scene data aggressively. For deployment on a small device, the code should distinguish static scene structure from dynamic values.

### Concrete measures

1. Separate static topology from dynamic state in Breakout scene generation:
   - static bricks/HUD/entity skeleton
   - dynamic transforms / visibility / text values

2. Investigate whether `with-expanded-collision-rules` can be done once per topology revision rather than on every publish.

3. Define when a full `FrameScene` rebuild is truly required versus when only a subset of entities changed.

4. If full rebuild remains necessary, document why and capture the measured heap/CPU budget explicitly.

### Review gate

- Either scene rebuild cost is substantially reduced, or the team has a documented justification backed by measurements.

## Workstream 8: Reduce code size and improve readability

### Problem

The code is functional, but several areas are large, duplicated, or difficult to audit quickly.

### Concrete measures

1. Split `src/game_demo_minifb.c` into smaller focused modules:
   - host window/backend glue
   - render-thread lifecycle
   - perf diagnostics
   - direct input adapter
   - main host application loop

2. Deduplicate the Breakout runtime logic between:
   - `libs/tiny-breakout/runtime.clj`
   - `libs/tiny-breakout/runtime-play.clj`

3. Keep host-only diagnostics and test-only helpers out of shared runtime files where possible.

4. Review `viewer_load_breakout_host_config_fast()` versus the generic config path and remove duplication that does not materially improve startup/heap behavior.

5. Prefer straightforward names and short ownership comments over clever indirection.

### Review gate

- File boundaries align with runtime responsibilities.
- The shipping path is smaller and easier to audit.
- Host-only code no longer dominates the shared deployment narrative.

## Workstream 9: Documentation and review notes

### Concrete measures

1. Add function-level documentation for the public C-side deployment entry points and lifecycle functions.

2. Add short comments where behavior is non-obvious:
   - why a queue is POD-only
   - why a callback may not allocate
   - why a thread is allowed or forbidden to touch Clj objects

3. Add one concise architecture note describing:
   - state publication
   - render handoff
   - collision dispatch
   - sound tick ownership

### Review gate

- A reviewer unfamiliar with the latest debugging history can still understand the runtime model by reading the docs and the top-level entry points.

## Test and measurement plan

Before implementation sign-off, require these checks:

1. Breakout startup and runtime regressions:
   - `src/tests/test_breakout_runtime_startup.c`
   - `src/tests/test_breakout_contract.c`
   - `src/tests/test_breakout_namespace_contract.c`

2. Sound-specific regressions:
   - `src/tests/test_sound_engine.c`
   - `src/tests/test_leak_sound_demos.c`

3. Threading / queue / event loop regressions:
   - `src/tests/test_event_loop_latency.c`
   - `src/tests/test_threading_macros.c`
   - `src/tests/test_lockfree_spsc_queue.c`

4. Add new focused tests for:
   - no Clj allocation on sound timer path
   - no Clj ownership operations on render thread
   - no unbounded queue growth during collision bursts
   - stable heap under repeated launch/input/collision cycles

5. Add a measurement step for ESP32 binary size:
   - compare `.text`, `.rodata`, and total image size before/after

6. Add a measurement step for hot-path heap behavior:
   - repeated launch
   - repeated paddle input
   - repeated brick collision
   - repeated sound SFX trigger

## Suggested implementation slices

To keep review manageable, split implementation into these PR-sized slices:

1. Debug/instrumentation removal only.
2. Thread/ownership contract + assertions/docs.
3. Audio callback detox.
4. Render-thread snapshot handoff.
5. Breakout runtime hot-path cleanup.
6. Collision-event shape reduction.
7. Scene rebuild reduction.
8. Code-size/readability refactor + final regression/size pass.

## Out of scope for this plan

- New gameplay features
- Visual polish unrelated to deployment fitness
- Rewriting the renderer itself without a measured deployment need
- Expanding the public sound API unless needed to support callback detox

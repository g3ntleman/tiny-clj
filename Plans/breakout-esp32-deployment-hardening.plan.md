---
name: Breakout ESP32 Deployment Hardening
overview: Prepare the Breakout runtime and deployment path for ESP32 shipment by prioritizing thread-safety of subjective-memory usage, removing hot-path allocations, reducing binary size, improving readability/documentation, and separating host-only diagnostics from shipping code.
todos:
  - id: baseline-review
    content: Review the current Breakout runtime, viewer, collision, and sound paths to capture concrete ESP32 deployment risks and define the execution order
    status: completed
  - id: remove-shipping-debug-noise
    content: Remove or isolate non-shipping debug instrumentation, absolute-path logging, and host-only diagnostics from sound and Breakout-adjacent runtime code
    status: completed
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
    status: in_progress
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
  - id: ingress-task-map-alloc
    content: Eliminate per-enqueue map allocation in event_loop_enqueue_ingress_call by using a fixed-slot ring buffer of POD task descriptors instead of heap-allocated CljPersistentMap per ingress entry
    status: pending
  - id: named-timer-heap-alloc
    content: Replace the malloc-based NamedTimerEntry linked list with a fixed-capacity static array to remove per-schedule heap allocation in the timer hot path
    status: pending
  - id: intern-symbol-caching
    content: Cache repeatedly called intern_symbol_global lookups in collision dispatch and scene bridge into static module-level variables initialized once, instead of repeated hash-table probes per frame or per hit
    status: completed
  - id: atom-swap-malloc
    content: Replace the per-call CLJ_MALLOC in atom_swap for the fn_args array with a small fixed-size stack buffer (covers the common 1-3 extra-args case) and only fall back to heap for rare large-arity swaps
    status: pending
  - id: collision-static-sizing
    content: Review and shrink VIEWER_COLLISION_RAW_HIT_CAP (512) and VIEWER_MAX_SPATIAL_RULES (128) to match actual Breakout usage, reducing static BSS footprint on ESP32
    status: pending
  - id: runtime-namespace-merge
    content: Merge runtime-play.clj back into runtime.clj to eliminate the indirection through runtime-play-call!/eval and the duplicated helper functions between the two namespaces
    status: pending
  - id: startup-eval-string-reduction
    content: Reduce the number of eval_string calls in viewer_load_breakout_host_config_fast by resolving multiple vars in a single eval expression or by using direct C namespace lookups
    status: pending
  - id: remove-cmake-dead-targets
    content: Clean up stale CMake targets such as the removed tiny-clj-profile and verify that only shipping and test targets remain
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

1. The sound tick path still crosses into Clj-object ownership and finished-event creation from the engine side, even though the old absolute-path debug logging has been removed.
2. The render thread still dereferences and retains/releases Clj scene objects directly.
3. The Breakout runtime still performs repeated dynamic work in hot paths (`require`, `eval`, full scene rebuilding, timer-spec construction).
4. Breakout runtime logic is still split across `runtime.clj` and `runtime-play.clj`, which increases maintenance cost and obscures the actual hot path.
5. The fast host config path still uses multiple `eval_string` lookups and a breakout-specific contract in C, even though symbol caching and schema loading are now cleaner.

### Already improved since the initial review

- Absolute-path debug file logging in the sound path has been removed from shipping-adjacent code.
- Collision symbol caching is implemented via `IdSymbolCacheEntry` tables in `viewer_collision_scene_bridge.c` and `viewer_collision_dispatch.c`.
- The tiny-fx record schema now comes from `libs/tiny-fx/gfx-records.clj`; C validates/loads that schema explicitly instead of registering fallback records on demand.

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

### Status update

Completed:

- The old absolute-path debug file writers have been removed from `src/sound_engine.c` and `src/sound_backend_host.c`.
- The remaining host sound diagnostics are integer/atomic based and no longer write ad-hoc files from shipping-adjacent code.

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

### Status update

Partially completed:

- `timeline-kick-timer-spec` is already stable/reusable.
- `runtime-play.clj` has extracted a chunk of direct input/runtime glue out of `runtime.clj`, which improved structure but did not remove the `runtime-play-call!` indirection.
- The hot path still contains dynamic namespace work (`runtime-play-call!` does `require` + `eval`), so this workstream remains open.

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

---

## 10. Ingress task-map heap elimination (`ingress-task-map-alloc`)

### Problem

Every `event_loop_enqueue_ingress_call` allocates a `CljPersistentMap` via `task_to_map` (4-key map: `:fn`, `:arg`, `:has-arg`, plus optional `:result-chan`). Collision bursts can fire dozens of these per frame.

### Measures

1. Replace the heap-allocated map with a POD struct (`EventLoopIngressSlot`) containing `fn`, `arg`, `has_arg` fields directly in the `g_event_loop_ingress_queue` ring buffer.
2. Only materialize the Clj map lazily when the task is drained to the interpreter-thread task queue.
3. Keep `event_loop_enqueue` (go-block path) unchanged since it needs the full map for result channels.

### Review gate

- No `make_map` or `map_transient` calls remain in the ingress-push path.
- Coalescing still works (compare fn + arg directly).

---

## 11. Named-timer heap elimination (`named-timer-heap-alloc`)

### Problem

`timer_named_set` does `CLJ_MALLOC(sizeof(NamedTimerEntry))` for each new named timer. Breakout schedules timers per ball-serve, per-segment watchdog, etc. The linked-list walk is also O(n) and allocation-heavy for an ESP32 hot path.

### Measures

1. Replace `NamedTimerEntry` linked list with a fixed `NamedTimerEntry g_named_timers[NAMED_TIMER_CAP]` array (capacity 8–16 is sufficient for Breakout).
2. Use a free-list index or simple linear scan for insert/remove.
3. Eliminate `CLJ_MALLOC`/`CLJ_FREE` from the schedule/cancel path.

### Review gate

- Zero `CLJ_MALLOC`/`CLJ_FREE` calls in the timer schedule/cancel path.
- Existing timer tests still pass.

---

## 12. intern_symbol_global caching (`intern-symbol-caching`)

### Problem

`viewer_collision_scene_bridge.c` calls `intern_symbol_global` **>15 times per collision dispatch cycle** for fixed, known keywords (`:prototype`, `:id`, `:slot`, `:kind`, `:channel`, `:radius`, `:self`, `:other`, `:a-id`, `:b-id`, `:collision`, etc.). Each call does a global hash-table lookup. The same pattern exists in `viewer_collision_dispatch.c` (`:enter`, `:exit`).

### Measures

1. Add a cache helper that hoists all collision-related keywords/symbols into static module-level IDs initialized once.
2. Replace all inline `intern_symbol_global` calls with the cached statics.
3. Reuse the same cache pattern for other modules with repeated global symbol lookups where it materially improves hot paths.

### Status update

Completed:

- `viewer_collision_scene_bridge.c` and `viewer_collision_dispatch.c` now use static `IdSymbolCacheEntry` tables with one-time initialization.
- The generic `IdSymbolCacheEntry` / `id_symbol_cache_init_global` pattern was pushed into shared symbol infrastructure and reused more broadly across the codebase.
- The old per-hit/per-dispatch `intern_symbol_global` churn in the collision bridge/dispatch path is gone.

### Review gate

- No `intern_symbol_global` calls remain in per-frame or per-hit code paths in collision scene bridge/dispatch.
- Record-descriptor lookups (`record_descriptor_lookup`) are also hoisted where possible.

---

## 13. atom_swap stack-buffer optimization (`atom-swap-malloc`)

### Problem

`atom_swap` allocates `fn_args` via `CLJ_MALLOC` for every swap call. In Breakout, `swap!` on `state*` and `held-buttons*` happens on every input and collision event – these are heap allocations in the hot path.

### Measures

1. Use a small stack-local `ID fn_args_buf[4]` for the common case (0–3 extra args).
2. Only fall back to `CLJ_MALLOC` if `extra_args_count > 3`.
3. Ensure `CLJ_FREE` is only called when the heap path was taken.

### Review gate

- `atom_swap` with 0–3 extra args causes zero heap allocations.
- All existing atom tests pass.

---

## 14. Collision static sizing review (`collision-static-sizing`)

### Problem

`VIEWER_COLLISION_RAW_HIT_CAP` is 512, `VIEWER_MAX_SPATIAL_RULES` is 128. Breakout has ~5 active spatial rules and at most ~20 simultaneous collision hits per frame. The over-provisioned arrays waste ~20KB of BSS on ESP32.

### Measures

1. Profile actual Breakout rule/hit counts via instrumented tests.
2. Lower caps to next-power-of-2 above the measured maximum (e.g. `RAW_HIT_CAP` → 64, `MAX_SPATIAL_RULES` → 16).
3. Add a compile-time `#if` or `_Static_assert` so ESP32 and host can differ if needed.

### Review gate

- All collision tests pass with reduced caps.
- No truncation warnings during a full Breakout play-through test.

---

## 15. Runtime namespace merge (`runtime-namespace-merge`)

### Problem

`runtime.clj` and `runtime-play.clj` still share too much semantic surface, and `runtime-play.clj` is still reached via dynamic `(require 'tiny-breakout.runtime-play)` plus `(eval (symbol ...))` in `runtime-play-call!`, adding latency and code size. The namespace split improved clarity somewhat, but did not yet eliminate the hot-path indirection.

### Measures

1. Merge `runtime-play.clj` back into `runtime.clj`.
2. Replace the `runtime-play-call!` indirection with direct function calls.
3. Remove the dynamic `require`/`eval` pattern from `apply-input!` and `on-spatial-event!`.
4. Delete the `runtime-play.clj` file.

### Review gate

- No `require` or `eval` calls remain in the Breakout hot path (input, collision, publish).
- All Breakout runtime tests pass unchanged.

---

## 16. Startup eval_string reduction (`startup-eval-string-reduction`)

### Problem

`viewer_load_breakout_host_config_fast` still uses multiple `eval_string` calls to resolve `scene*`, `bootstrap-runtime!`, `start-runtime!`, and `on-spatial-event!`. Each `eval_string` parses, compiles, and evaluates a full Clojure expression.

### Measures

1. Combine into a single `eval_string` that returns a vector/map of all needed values.
2. Or use a direct C-level namespace-var lookup (`namespace_resolve_var`) to avoid the eval overhead entirely.
3. Keep the existing `WITH_AUTORELEASE_POOL` scoping.

### Review gate

- Startup allocations measurably reduced (heap profile test).
- Config load still succeeds in all test scenarios.

---

## 17. CMake dead-target cleanup (`remove-cmake-dead-targets`)

### Problem

The `tiny-clj-profile` target was already removed from CMakeLists.txt in the previous commit. There may be other stale or dead targets, outdated source lists, or orphaned `#ifdef` blocks in the build system.

### Measures

1. Audit `CMakeLists.txt` for targets that reference removed files or are no longer built.
2. Remove any leftover `add_executable`/`add_library` entries for dead targets.
3. Verify `make` and `make test` still succeed with a clean build.

### Review gate

- `cmake --build build` succeeds with no warnings about missing sources.
- Only shipping (`tiny-clj`, `tiny-clj-host`, `unit-tests`) and known test targets remain.

---

## Suggested implementation slices

To keep review manageable, split implementation into these PR-sized slices:

1. Debug/instrumentation removal only.
2. Thread/ownership contract + assertions/docs.
3. Audio callback detox.
4. Render-thread snapshot handoff.
5. Breakout runtime hot-path cleanup (includes runtime namespace merge + startup eval reduction).
6. Collision-event shape reduction + static sizing.
7. Ingress task-map + named-timer + atom_swap heap elimination.
8. Scene rebuild reduction.
9. Code-size/readability refactor + CMake cleanup + final regression/size pass.

## Out of scope for this plan

- New gameplay features
- Visual polish unrelated to deployment fitness
- Rewriting the renderer itself without a measured deployment need
- Expanding the public sound API unless needed to support callback detox

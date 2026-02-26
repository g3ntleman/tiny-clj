# STATUS: IN_PROGRESS
# When this plan is fully implemented, change this header to:
#   STATUS: DONE (YYYY-MM-DD) — <commit>
#
# Rule: If a plan is completely implemented, mark it clearly at the top.
#
# ESP32 Serial REPL + core.async (current plan) and go/state-machine (later)

This plan captures two tracks:

- **Track A (current priority)**: make the Tiny-CLJ REPL usable over ESP32 UART with line-editing and integrate the existing minimal `clojure.core.async` subset (channels + callbacks) with the event loop and GPIO.
- **Track B (later)**: implement `go`/`go-loop` via an IOC/state-machine macro pipeline (inspired by Clojure/ClojureScript), once the runtime hooks exist.

The repository rule is “macOS-first”: everything should be testable on macOS before moving to ESP32.

## Progress update (2026-02-26)

- Added minimal `go` support in `libs/clojure/core/async.clj`:
  - `(go (<! ch))` parks and resumes via callback.
  - `(go (>! ch v))` waits for put callback completion.
  - `(go expr...)` runs asynchronously via `clojure.core/schedule`.
- Added parking script test: `libs/test/core_async/parking.clj`.
- Hooked test into `src/tests/test_plan_trackA_scripts.c`:
  - `test_plan_trackB_core_async_parking_script`.
- Verified green on macOS:
  - `./build/unit-tests --test "test_plan_trackA_scripts/*core_async*"`
  - Result: 5 tests, 0 failures.

---

## Track A — ESP32 serial REPL + core.async subset (do this first)

### A0. Ground rules / invariants
- Single-threaded execution model (event loop driven).
- No hidden background threads.
- UART input must support an interactive REPL UX (editing + history).
- core.async subset must keep **names + arities 1:1** for included vars/macros, and throw on unsupported features.
- Provide a stable API surface usable from scripts and tests on macOS.

### A1. Serial REPL transport (ESP32 UART)
- Introduce a platform abstraction for:
  - **read byte(s)** with timeout/non-blocking option
  - **write bytes**
  - **flush**
- Ensure the REPL can be driven by a “character stream” interface (so macOS can simulate UART).

Deliverables:
- UART-backed implementation for ESP32.
- macOS “pipe/pty/stdio” implementation for test harnesses.

### A2. Line editing over serial
Goal: reuse the existing line editor but make it work over UART.

Tasks:
- Audit `line_editor` integration points (read/write functions, escape sequences).
- Ensure terminal control sequences degrade gracefully on dumb terminals (or provide a “minimal mode”).
- History persistence can be optional; in-memory history is sufficient for ESP32.

### A3. Event loop integration
Goal: unify “REPL input”, “core.async deliveries”, and “GPIO events” under one event loop.

Tasks:
- Define:
  - `event_loop_run_once(...)`
  - `event_loop_schedule(...)`
  - `event_loop_watch_fd/uart(...)` (platform-specific)
- Ensure core.async uses the event loop for fairness (even if callbacks are “on-caller” today).

### A4. GPIO reactions
Goal: surface GPIO changes as events into Clojure.

Two viable APIs:
- **Channel-first**: expose a `gpio-channel` producing edge events.
- **Callback-first**: expose `on-gpio!` that schedules a Clojure fn on edge.

Preference: channel-first (composes with existing core.async subset).

### A5. Tests (macOS)
- Add a test script that:
  - Creates a channel, registers callbacks, and ensures delivery order is stable.
  - Simulates “GPIO events” using a fake platform hook.
  - Drives the REPL using a simulated character stream and verifies editing/history basics.
- Status:
  - Core async scripts exist and pass (`smoke`, `callbacks`, `go_unsupported`, `parking`).
  - GPIO channel script exists and passes.
  - REPL character-stream editing/history test is still open.

---

## Track B — go/go-loop via IOC/state-machine (do this later)

### B0. Reality check
“Copying” ClojureScript macros 1:1 will not work until the required runtime hooks exist.
However, the **approach** is portable: compile `go` bodies into an explicit state machine.

### B1. Minimal runtime contract required
Implement a tiny runtime API that the generated state machine can use:

- A “process/state” object holding:
  - current state index
  - locals frame (vector/array)
  - exception slot
- Operations:
  - `take!` / `put!` that park and resume
  - `set-state!`, `get-state`, `set-value!`, `get-value`
  - `return!` / `finished?`
  - exception edges (try/catch/finally in the machine)
- Scheduling:
  - `run` / `dispatch` on the Tiny-CLJ event loop

### B2. Macro pipeline
Option 1 (closest to CLJS): implement `clojure.core.async.impl.ioc-macros` subset.
Option 2 (simpler): implement a Tiny-CLJ-specific macro that emits a state machine directly.

Recommendation: start with Option 2, then converge toward Option 1 only if needed.

### B3. Compatibility surface
When `go` exists, also consider:
- `<!`, `>!`, `alts!` (at least throw with good messages until implemented)
- cancellation/timeouts (`timeout`) (likely requires timer integration in event loop)

### B4. Test plan for go/go-loop
- “smoke” test: `(go 1)` returns a channel delivering `1`.
- parking test: `<!` from empty channel parks and later resumes.
- exception test: throw inside `go` propagates as channel close + error payload (define semantics).
- Status:
  - `smoke` and `parking` are now covered by script tests.
  - Exception semantics are still open and need explicit test contract.


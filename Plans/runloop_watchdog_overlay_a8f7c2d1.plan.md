# Runloop Watchdog Overlay Plan

## Goal

Add a lightweight host-side runloop liveness monitor for the macOS Breakout host.
It should detect when the runloop has stopped ticking, show a native overlay warning,
and hide the warning again once progress resumes.

## Constraints

- Test first and land behavior in small steps.
- Keep the hot path cheap: direct atomics, no extra watchdog thread.
- Main-thread/UI logic should live in the macOS host layer.
- Initial scope is diagnostic only: no recovery, no forced restart.

## Steps

### 1. Add a small runloop liveness model

- Introduce atomics for:
  - `last_runloop_tick_ns`
  - `runloop_iteration_count`
- Update them directly from the host runloop thread on each iteration.
- Add a tiny read-only snapshot/helper API for consumers.

Tests:
- unit test for initial/reset state
- unit test that a tick updates timestamp and iteration count
- unit test for status classification thresholds

Status: completed

### 2. Add a macOS watchdog state machine without UI

- Add a macOS-side polling/check function that:
  - reads the runloop snapshot
  - classifies `healthy` vs `stalled`
  - tracks visibility transitions
- Keep this API testable without needing a real `NSView`.

Tests:
- unit test that a stalled state requests showing the warning
- unit test that recovery requests hiding it again
- unit test that repeated stalled checks do not spam repeated activation

Status: completed

### 3. Attach the watchdog to the macOS main runloop

- Install a periodic main-thread timer (every ~3 seconds).
- On each fire:
  - check runloop liveness
  - lazily create/show overlay if stalled
  - hide overlay if recovered
- Keep the overlay native to macOS (`NSView`) and independent of framebuffer diagnostics.

Tests:
- host startup test that timer/watchdog activation hook is installed
- host startup test that overlay hook is not shown at boot when healthy

Status: completed

### 4. Validate in the host app

- Build `unit-tests`
- Run the targeted startup/runloop tests
- Manually verify:
  - overlay appears on stall
  - overlay disappears on recovery

Tests:
- `test_breakout_runtime_startup_*runloop*`
- any new watchdog-specific tests

Status: in_progress

Progress note:
- targeted watchdog tests are green
- manual stall/recovery verification in the actual app is still open

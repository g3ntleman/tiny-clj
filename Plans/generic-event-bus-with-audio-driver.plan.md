---
name: Generic Event Bus with Audio Finished Driver
overview: >
  Replace the current single global sound finished callback and the source-specific
  routing in `tiny-clj.event` with a fully generic event bus. Audio finished events
  are the driving first use-case, but the target architecture is a shared bus for
  all event sources. The change must be incremental, test-first, and must not keep
  Breakout-specific callback logic inside `tiny-breakout.audio`. Supported bus
  semantics should follow a small, explicit Clojure/JVM-aligned subset, event maps
  should already be in their final public delivery shape, and the hot path must stay
  lightweight enough for ESP targets. The legacy partial `clojure.core.async`
  implementation must be disabled as part of this migration so the repo does not
  keep two competing async/pub-sub models alive.
todos:
  - id: phase-1-red-generic-event-bus-contract
    content: "RED: Add focused tests for the supported generic event-bus subset (Clojure/JVM-aligned subscribe, unsubscribe, source/id routing, final event shape, drain timing) using audio finished as the first end-to-end driver"
    status: pending
  - id: phase-2-green-bus-core
    content: "GREEN: Add the generic event-bus core with one clear lightweight dispatch path, reusing the existing event-loop transport unless replacement is required to preserve semantics cleanly"
    status: pending
  - id: phase-3-green-audio-adapter
    content: "GREEN: Migrate sound finished notifications onto the generic event bus as the first real source adapter, emitting final public event maps directly"
    status: pending
  - id: phase-4-red-existing-source-parity-tests
    content: "RED: Add or tighten tests proving existing event sources still behave correctly when routed through the generic event bus with the same public payload shape"
    status: pending
  - id: phase-5-green-existing-source-migration
    content: "GREEN: Move button/sensor/spatial/timeline routing behind the generic event-bus architecture without changing public semantics"
    status: pending
  - id: phase-6-red-app-integration-tests
    content: "RED: Add app-level tests proving Breakout can subscribe to startup-song finished through the generic event bus without depending on tiny-fx.sound-demos"
    status: pending
  - id: phase-7-green-breakout-migration
    content: "GREEN: Migrate tiny-breakout.audio/runtime to the generic event bus and remove any Breakout-local finished-hook logic"
    status: pending
  - id: phase-8-regression-and-budgets
    content: "REFACTOR: Re-run sound, event, runtime, and heap regressions; verify no new ignored tests, no behavior regressions, and no extra startup namespace loads"
    status: pending
  - id: cleanup
    content: Sourcecode aufräumen – Debug-Code, temporäre Workarounds, tote Codepfade, überflüssige Kommentare und nicht mehr benötigte Hilfsfunktionen entfernen
    status: pending
isProject: false
---

# Generic Event Bus Plan

## Goal

Turn the current source-specific event routing into a fully generic event bus
that all event sources can publish to and all apps can subscribe to through a
single shared contract. Audio finished notifications are the driving first
end-to-end use-case, but the target is not audio-only.

The supported public semantics should be as close to Clojure/JVM as feasible
within a deliberately small, explicit subset. Unsupported areas must fail
clearly rather than approximating broader `core.async` behavior.

The result should support:

- one generic subscribe/unsubscribe model for all event sources
- source/id-based routing without source-specific hardcoding in `tiny-clj.event`
- per-song subscriptions by `track-id`
- deterministic unsubscribe/removal
- clean behavior for natural completion vs manual stop
- reuse by Breakout and future apps without new app-specific callback plumbing
- final public event maps that can later serve directly as channel content for a
  larger `core.async`-style subset
- explicit disabling of the current legacy `clojure.core.async` subset so the
  generic event bus becomes the only supported pub/sub path

## Hard constraint

- Never add polling loops. Audio finished delivery and cleanup must remain
  strictly event-driven via the existing sound/event-loop path.
- Keep the hot path lightweight enough for time-critical ESP event delivery.
- Avoid extra adapter hops, wrapper event shapes, or mixed fallback dispatch
  paths in the steady-state runtime path.

## Current State

Today the sound engine exposes one global callback hook, while `tiny-clj.event`
still knows about individual source types and delegates to them explicitly.

On the audio side:

- public Clojure API: `tiny-fx.sound/sound-on-finished!`
- native storage: one retained function in `sound_engine.c`
- event payload already exists and includes `:source`, `:kind`, and `:track-id`

This is enough for one global observer, but not ideal for app-level
composition, and it does not solve the broader architecture issue:

- app code must coordinate a shared global callback
- multiple song-specific listeners are awkward
- `tiny-clj.event` currently hardcodes source-specific routing instead of being a
  generic bus
- `libs/clojure/core/async.clj` provides an older partial async/channel model
  that would overlap conceptually with the planned generic bus and should not
  remain enabled as a second in-repo direction

## Scope

In scope:

- `libs/tiny-clj/event.clj`
- `libs/clojure/core/async.clj`
- `libs/tiny-fx.sound.clj` only if a small API adjustment is needed
- sound-engine callback routing on the C side and/or shared Clojure event layer
- a generic event-bus core and source-registration/routing model
- a small explicit supported subset whose semantics match Clojure/JVM as closely
  as feasible
- migration of existing sources onto that generic model
- disabling the legacy `clojure.core.async` subset so it fails clearly instead of
  continuing to expose a second partial async model
- tests in `src/tests/test_sound_engine.c`, `src/tests/test_breakout_runtime_startup.c`,
  nearby event-related tests, and any nearby existing test groups
- migration of Breakout startup-song cleanup to the shared event API

Out of scope:

- adding polling loops
- adding new test files/groups without a strong need
- changing public semantics of existing sources unless covered by explicit red
  tests in this plan
- cross-cutting refactors outside event delivery and its direct app integrations
- pretending to support broader `core.async` surface area than is actually
  implemented
- keeping the old partial `clojure.core.async` implementation alive beside the
  generic bus

## Target Architecture

### Generic bus model

All event producers publish into one shared event-bus path.

All subscribers register through one shared subscription contract.

The bus routes events by at least:

- `:source`
- `:id`

The preferred implementation reuses the existing event-loop as transport and
execution core because it already provides a generic `fn` + optional payload
dispatch path. If that path cannot preserve the intended semantics cleanly or
without unnecessary complexity, a targeted replacement of the current
scheduler/event-loop path is allowed.

Decision rule:

1. Prefer integration when the existing event-loop can carry the supported
   semantics through one clear dispatch path.
2. Replace the current scheduler/event-loop path only if integration would force
   unclear fallback behavior, source-specific special cases, or avoidable
   semantic deviations from the supported Clojure/JVM-aligned subset.

### Supported public semantics

The first supported subset should stay intentionally small and explicit:

- one shared subscribe/unsubscribe contract
- source/id-based event matching
- deterministic remove-before-drain behavior
- event delivery on the existing push-based execution model
- one clear supported direction for pub/sub: the generic event bus, not the
  current legacy `clojure.core.async` subset

This plan does not require a full `core.async` implementation. The goal is
compatible semantics for the supported pub/sub-like cases, not channel,
buffering, go-block, or alts semantics. The existing partial
`clojure.core.async` namespace should therefore be disabled so unsupported
semantics fail clearly instead of coexisting with the bus.

### Event model

Natural sound completion remains the first concrete driver and should surface as
a bus event with stable final public shape:

```clojure
{:source :audio
 :kind :finished
 :track-id <song-id>}
```

Other existing sources should also map into the same bus contract, preserving
their current observable semantics.

Payload contract:

- emitted event maps must already be in their final public delivery shape
- the same event map shape must be suitable for direct subscriber delivery and
  future channel content in a larger `core.async`-style subset
- the bus must not require a second semantic event representation or a
  post-publication translation layer
- source adapters may populate the final event map, but should not wrap it in an
  extra bus-specific envelope

Minimum common event identity fields:

- `:source`
- `:id` when a source already has or can derive stable subscriber identity
- `:kind` when the source exposes semantic event kinds

Audio-specific identity remains:

- source-level public event shape may still carry `:track-id`
- generic routing should treat that track identity consistently as the audio
  source `:id` match key

### Hot-path rule

The generic bus must add only a very small amount of work to the current
event-loop path:

- determine routing identity
- find matching subscribers
- deliver the final event map

Avoid in the steady-state runtime path:

- extra wrapper maps
- duplicate event normalization passes
- per-event adapter chains across multiple old APIs
- extra scheduling hops unless required by already-tested semantics

### Subscription model

Apps subscribe through `tiny-clj.event`, for example conceptually:

```clojure
(event/on {:source :audio :id :startup/the-entertainer} handler)
```

Interpretation for audio:

- `:source :audio`
- `:id` maps to `:track-id`
- the shared bus routes only matching finished events to that watcher

### Semantic rules

1. Natural completion emits one `:finished` event.
2. Manual `sound-stop-track!` does not emit `:finished` unless explicitly changed
   by a later design decision. Current behavior should remain stable.
3. Removing a watcher before drain prevents callback execution.
4. Multiple registered song watchers must not interfere with each other.
5. Existing non-audio sources keep their public behavior while moving behind the
   generic bus.
6. Shared audio subscription support must not require `tiny-fx.sound-demos`.
7. Supported semantics should stay compatible with the chosen Clojure/JVM-style
   subset; unsupported areas fail clearly.

## Test-First Phases

### Phase 1 - RED: Generic event-bus contract

Add failing tests first.

1. One shared subscribe/unsubscribe contract can route events by `:source` and
   `:id`.
2. The supported subset behaves like the intended Clojure/JVM-aligned semantics
   in the covered cases and fails clearly outside them.
3. Audio finished events can be observed as the first concrete source by
   `track-id`.
4. The observed audio event map keeps the expected final public shape and can be
   treated directly as future channel content:
   - `:source :audio`
   - `:kind :finished`
   - `:track-id <id>`
5. Two subscriptions for different songs only fire for their own song.
6. Unsubscribing before event-loop drain prevents callback execution.
7. Manual `sound-stop-track!` still does not dispatch a finished event.
8. Requiring or calling the legacy `clojure.core.async` subset fails clearly
   after the migration instead of exposing stale partial semantics.

Prefer extending existing sound-engine tests rather than adding a new test file.

Exit criterion:

- these tests fail against the current implementation because there is not yet a
  fully generic event-bus contract.

### Phase 2 - GREEN: Generic bus core

Implement the smallest shared mechanism that can route events generically.

Design constraints:

- no Breakout-specific state in the shared routing layer
- no compatibility wrapper left behind
- keep one clear event publication and dispatch path
- keep the steady-state hot path lightweight enough for ESP event timing
- prefer reusing the existing event-loop transport unless tests prove that clean
  semantics require replacing it
- disable the legacy `clojure.core.async` implementation rather than leaving it
  as a parallel partial solution

Likely implementation shape:

1. introduce one generic watch/subscription registry
2. introduce one generic publish/dispatch path
3. make event payloads final at publication time so no second translation layer
   is required
4. keep dispatch on the existing event loop so callback timing matches current
   tests, unless a targeted replacement is required by the supported semantics
5. make watcher removal deterministic and safe before drain
6. replace the current `clojure.core.async` entry points with clear unsupported
   failures or equivalent explicit disable behavior, so there is no dual-path
   async API in the repo

Exit criterion:

- phase-1 generic-bus tests turn green without touching Breakout-specific logic yet
- the old `clojure.core.async` path is no longer an active supported runtime path

### Phase 3 - GREEN: Audio source adapter on the generic bus

Make audio finished notifications publish into the generic bus.

Steps:

1. bridge the existing sound finished callback into generic bus publication
2. define how audio `:id` maps to song identity (`:track-id`)
3. emit the final public event map directly from the audio path
4. preserve current queue/drain timing
5. document that `:audio` finished notifications are push-based and do not poll

Tests to add or update first if needed:

- `event/on` with `{:source :audio :id ...}` subscribes
- `nil` callback removes the subscription
- unsupported audio option shapes fail clearly

Exit criterion:

- app code can use `tiny-clj.event` for audio exactly like the other supported
  sources

### Phase 4 - RED: Existing source parity tests

Add failing tests before migrating the remaining existing sources.

1. `:button` subscriptions still behave as before.
2. `:sensor` subscriptions still behave as before.
3. `:spatial` subscriptions still behave as before.
4. `:timeline` subscriptions still behave as before.
5. Their observed event payloads remain in the same public shape expected by app
   callbacks.
6. Unsubscribe semantics remain stable across all migrated sources.

Prefer existing nearby tests instead of creating a new broad event-bus suite if
the current groups can express the contracts.

Exit criterion:

- parity tests fail until the old source-specific routing is moved behind the
  generic bus

### Phase 5 - GREEN: Existing source migration

Move existing sources behind the generic event-bus architecture.

Steps:

1. migrate `button`, `sensor`, `spatial`, and `timeline` source routing onto the
   generic bus
2. keep public `tiny-clj.event` semantics stable
3. make each source publish final public event maps directly into the shared bus
4. remove redundant source-specific routing logic where the generic path replaces it
5. remove or disable any remaining old async/pub-sub entry points that would
   duplicate the new bus direction

Constraints:

- preserve public behavior unless a red test explicitly changes it
- no fallback dual-path architecture left behind
- no source-specific compatibility wrappers that outlive the migration
- no still-enabled legacy `clojure.core.async` path left behind

Exit criterion:

- all currently supported sources route through the generic bus model

### Phase 6 - RED: App integration tests

Add failing app-level tests before migrating Breakout.

1. `tiny-breakout.runtime/start-runtime!` can subscribe to startup-song finished
   through the generic event bus.
2. Breakout startup still does not autoload `tiny-fx.sound-demos`.
3. Startup-song cleanup logic can be triggered by a finished event for
   `:startup/the-entertainer`.
4. Non-matching song completions do not trigger Breakout cleanup.

Use the existing Breakout startup test group.

Exit criterion:

- tests fail until Breakout is migrated away from ad hoc finished-hook logic

### Phase 7 - GREEN: Breakout migration

Migrate Breakout to the generic event bus.

Steps:

1. keep `tiny-breakout.audio` responsible only for playing/loading its tracks
2. move finished-event subscription usage to the correct shared integration point
3. subscribe per startup song using `tiny-clj.event`
4. run cleanup when that specific song finishes
5. avoid `tiny-fx.sound-demos` dependency in runtime path

Constraints:

- no app-specific callback registry in Breakout
- no global callback re-registration race in app code
- preserve current startup behavior unless a test intentionally changes it

Exit criterion:

- Breakout uses only the generic event-bus path for startup-song completion

### Phase 8 - REFACTOR: Regression and budgets

After behavior is green:

1. re-run focused sound-engine tests
2. re-run event-source parity tests
3. re-run Breakout startup/runtime tests
4. re-run sound-demo and heap-sensitive groups that could regress indirectly
5. run full unit tests
6. compare namespace-load and startup-heap baselines before/after
7. verify legacy `clojure.core.async` calls now fail clearly and predictably

Acceptance checks:

- `0 Failures`
- no increased ignored-test count
- no regression in event-loop delivery semantics
- no new mandatory dependency from Breakout runtime to `tiny-fx.sound-demos`
- no still-enabled legacy `clojure.core.async` runtime path

## File Plan

Likely files to touch:

- `libs/tiny-clj/event.clj`
- `libs/clojure/core/async.clj`
- `libs/tiny-breakout/audio.clj`
- `libs/tiny-breakout/runtime.clj`
- `libs/tiny-fx/sound.clj` only if public API docs need updates
- `src/sound_engine.c`
- event-source-specific helpers currently behind `tiny-clj.event`
- `src/tests/test_sound_engine.c`
- `src/tests/test_breakout_runtime_startup.c`

Possible additional nearby files, only if required by the final design:

- `src/builtins_sound.c`
- `src/symbol.c`
- `src/symbol.h`

## Risks

1. Global callback snapshot behavior may hide watcher updates made after the
   finished event is queued.
   Mitigation:
   - preserve queue/drain semantics in tests
   - explicitly test subscribe/unsubscribe timing around drain

2. Multiple songs finishing close together may expose routing bugs.
   Mitigation:
   - add a two-song dispatch isolation test early

3. Breakout cleanup might accidentally rely on demo-specific namespaces again.
   Mitigation:
   - keep the autoload regression test in the Breakout startup suite

4. Over-generalizing `tiny-clj.event` could make the API harder to reason about.
   Mitigation:
   - keep the generic bus contract small and explicit
   - map source-specific identity into `:id` consistently
   - avoid extra option paths unless tests prove a need

5. A new bus layer could add too much overhead on ESP hot paths.
   Mitigation:
   - keep the event-loop transport path direct
   - publish final public event maps once
   - avoid adapter chains and extra wrapper allocations

6. Future `core.async` ambitions could accidentally leak unsupported semantics
   into the first implementation.
   Mitigation:
   - document the supported subset explicitly
   - add red tests only for supported semantics
   - make unsupported cases fail clearly
   - disable the current legacy `clojure.core.async` subset during migration so
     there is only one supported direction

## Acceptance Criteria

1. `tiny-clj.event` is backed by a fully generic event-bus architecture rather
   than source-specific hardcoded routing.
2. The supported public semantics match the intended small Clojure/JVM-aligned
   subset as covered by tests.
3. Existing supported sources keep their public behavior on top of that generic
   bus.
4. Event payloads are already in their final public shape and can later serve as
   direct channel content for an expanded `core.async`-style subset.
5. The legacy partial `clojure.core.async` implementation is disabled and fails
   clearly instead of remaining as a competing runtime path.
6. Any app can subscribe to a song-finished event through `tiny-clj.event`.
7. Subscriptions are per song (`track-id`), not one global app-level callback.
8. Natural completion dispatches exactly one callback for matching subscribers.
9. Manual stop keeps current non-finished behavior unless explicitly changed by
   failing tests and an approved design decision.
10. Breakout startup-song cleanup uses the shared event API and does not depend on
   `tiny-fx.sound-demos`.
11. The chosen execution path stays push-based, avoids polling, and keeps the
    additional dispatch layer lightweight enough for ESP event timing.
12. All relevant tests and the full suite are green.

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
  lightweight enough for ESP targets. The current partial `clojure.core.async`
  implementation is not the desired target shape; it should be moved to a backup
  location and replaced by a new small, Clojure-compatible pub/sub subset inside
  the `clojure.core.async` namespace itself.
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

The public home for that subset should be `clojure.core.async`. Existing
`tiny-clj.event` call sites may stay as migration scaffolding for now, but the
new shared pub/sub model should live in `clojure.core.async` using compatible
names, arities, and semantics for the supported surface.

The result should support:

- one generic subscribe/unsubscribe model for all event sources
- source/id-based routing without source-specific hardcoding in `tiny-clj.event`
- per-song subscriptions by `track-id`
- deterministic unsubscribe/removal
- clean behavior for natural completion vs manual stop
- reuse by Breakout and future apps without new app-specific callback plumbing
- final public event maps that can later serve directly as channel content for a
  larger `core.async`-style subset
- a new supported pub/sub path in `clojure.core.async`
- moving the current legacy `clojure.core.async` implementation into backup so
  the new pub/sub subset does not compete with the old channel/go experiment

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
- `libs/clojure/core/async.clj` currently contains an older partial
  channel/go-oriented experiment; the implemented parts are not the desired
  final subset for this task

## Scope

In scope:

- `libs/tiny-clj/event.clj`
- `libs/clojure/core/async.clj`
- backup location for the old `clojure.core.async` implementation inside the repo
- `libs/tiny-fx.sound.clj` only if a small API adjustment is needed
- sound-engine callback routing on the C side and/or shared Clojure event layer
- a generic event-bus core and source-registration/routing model
- a small explicit supported subset whose semantics match Clojure/JVM as closely
  as feasible
- migration of existing sources onto that generic model
- replacing the current `clojure.core.async` contents with a new compatible
  pub/sub subset while keeping the old implementation only as backup
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
- keeping the old partial `clojure.core.async` implementation active as the
  runtime implementation after the new subset lands

## Target Architecture

### Generic bus model

All event producers publish into one shared event-bus path.

All subscribers register through one shared subscription contract.

The intended public contract for the new bus lives in `clojure.core.async`,
not in a project-specific namespace. `tiny-clj.event` may delegate into that
contract where useful during migration, but should not remain the long-term
canonical API if the same semantics are exposed through `clojure.core.async`.

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

- `pub`
- `sub`
- `unsub`
- `unsub-all`
- deterministic remove-before-drain behavior
- event delivery on the existing push-based execution model

This plan does not require a full `core.async` implementation. The goal is
compatible semantics for the supported pub/sub-like cases, not full channel,
buffering, go-block, timeout, or alts semantics. The existing partial
`clojure.core.async` namespace should therefore be moved aside into backup and
replaced by the new supported subset. Any function that remains implemented in
`clojure.core.async` must be compatible in name, arity, and covered behavior;
unsupported areas fail clearly.

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

Apps should be able to subscribe through `clojure.core.async`, for example
conceptually:

```clojure
(let [src (clojure.core.async/pub event-source :source)
      out (clojure.core.async/chan)]
  (clojure.core.async/sub src :audio out))
```

Interpretation for audio:

- `:source :audio`
- event payloads still carry `:track-id`
- finer filtering like per-song cleanup can be done on the delivered final event map

### Semantic rules

1. Natural completion emits one `:finished` event.
2. Manual `sound-stop-track!` does not emit `:finished` unless explicitly changed
   by a later design decision. Current behavior should remain stable.
3. Removing a subscription before drain prevents callback execution.
4. Multiple registered subscribers must not interfere with each other.
5. Existing non-audio sources keep their public behavior while moving behind the
   generic bus.
6. Shared audio subscription support must not require `tiny-fx.sound-demos`.
7. Implemented `clojure.core.async` functions stay compatible in the covered
   behavior and arities; unsupported parts fail clearly.

## Test-First Phases

### Phase 1 - RED: Generic core.async pub/sub contract

Add failing tests first.

1. `clojure.core.async/pub`, `sub`, `unsub`, and `unsub-all` exist with the
   intended compatible arities.
2. The supported subset behaves like the intended Clojure/JVM-aligned semantics
   in the covered cases and fails clearly outside them.
3. Audio finished events can be observed as the first concrete source through
   the new pub/sub path.
4. The observed audio event map keeps the expected final public shape and can be
   treated directly as channel content:
   - `:source :audio`
   - `:kind :finished`
   - `:track-id <id>`
5. Two subscribers with different filtering do not interfere with each other.
6. `unsub` before event-loop drain prevents callback execution.
7. Manual `sound-stop-track!` still does not dispatch a finished event.
8. The old `clojure.core.async` implementation is preserved only in backup and
   is no longer the active runtime implementation.

Prefer extending existing sound-engine tests rather than adding a new test file.

Exit criterion:

- these tests fail against the current implementation because there is not yet a
  compatible `clojure.core.async` pub/sub contract for the supported subset.

### Phase 2 - GREEN: New core.async pub/sub core

Implement the smallest shared mechanism that can route events generically.

Design constraints:

- no Breakout-specific state in the shared routing layer
- no compatibility wrapper left behind
- keep one clear event publication and dispatch path
- keep the steady-state hot path lightweight enough for ESP event timing
- prefer reusing the existing event-loop transport unless tests prove that clean
  semantics require replacing it
- move the old `clojure.core.async` implementation into backup before replacing
  the active file with the new subset
- every implemented `clojure.core.async` function must keep compatible name,
  arity, and covered behavior

Likely implementation shape:

1. move the current `libs/clojure/core/async.clj` to a backup path in-repo
2. replace active `libs/clojure/core/async.clj` with a small compatible
   pub/sub-focused subset
3. introduce one generic publication/subscription registry
4. introduce one generic publish/dispatch path
5. make event payloads final at publication time so no second translation layer
   is required
6. keep dispatch on the existing event loop so callback timing matches current
   tests, unless a targeted replacement is required by the supported semantics
7. make subscription removal deterministic and safe before drain

Exit criterion:

- phase-1 pub/sub tests turn green without touching Breakout-specific logic yet
- the old `clojure.core.async` implementation exists only as backup, not as the
  active runtime file

### Phase 3 - GREEN: Audio source adapter on the new pub/sub core

Make audio finished notifications publish into the generic bus.

Steps:

1. bridge the existing sound finished callback into generic bus publication
2. define the topicing strategy used by `pub` for final event maps
3. emit the final public event map directly from the audio path
4. preserve current queue/drain timing
5. document that `:audio` finished notifications are push-based and do not poll

Tests to add or update first if needed:

- `clojure.core.async/pub` + `sub` observe finished audio events
- `unsub` removes the subscription
- unsupported audio option shapes fail clearly

Exit criterion:

- app code can observe audio events through the new `clojure.core.async`
  pub/sub subset

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
2. keep public behavior stable for existing event sources
3. make each source publish final public event maps directly into the shared bus
4. adapt `tiny-clj.event` to delegate into the new shared model where needed
5. remove redundant source-specific routing logic where the generic path replaces it

Constraints:

- preserve public behavior unless a red test explicitly changes it
- no fallback dual-path architecture left behind
- no source-specific compatibility wrappers that outlive the migration
- no still-enabled legacy `clojure.core.async` implementation left behind as the
  active runtime file

Exit criterion:

- all currently supported sources route through the generic bus model

### Phase 6 - RED: App integration tests

Add failing app-level tests before migrating Breakout.

1. `tiny-breakout.runtime/start-runtime!` can subscribe to startup-song finished
   through the new `clojure.core.async` pub/sub path.
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
3. subscribe per startup song using the new `clojure.core.async` pub/sub subset
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
7. verify the new supported `clojure.core.async` subset behaves compatibly in the
   covered cases
8. verify the backup copy of the old implementation is not used by runtime code

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
- backup path for legacy `core.async`
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
   - keep the first active subset small: `pub`, `sub`, `unsub`, `unsub-all`
   - move the old implementation to backup before replacing the active file

## Acceptance Criteria

1. `clojure.core.async` provides a small active pub/sub subset with compatible
   names, arities, and covered semantics.
2. The supported public semantics match the intended small Clojure/JVM-aligned
   subset as covered by tests.
3. Existing supported sources keep their public behavior on top of that generic
   bus.
4. Event payloads are already in their final public shape and can later serve as
   direct channel content for an expanded `core.async`-style subset.
5. The previous partial `clojure.core.async` implementation has been moved to an
   in-repo backup location and is no longer the active runtime implementation.
6. Any app can subscribe to a song-finished event through the shared pub/sub API.
7. Subscriptions are per song (`track-id`), not one global app-level callback.
8. Natural completion dispatches exactly one callback for matching subscribers.
9. Manual stop keeps current non-finished behavior unless explicitly changed by
   failing tests and an approved design decision.
10. Breakout startup-song cleanup uses the shared event API and does not depend on
   `tiny-fx.sound-demos`.
11. The chosen execution path stays push-based, avoids polling, and keeps the
    additional dispatch layer lightweight enough for ESP event timing.
12. All relevant tests and the full suite are green.

---
name: ESP32 Vector Header Shrink and Transient Ringbuffer
overview: >
  Reduce `CljPersistentVector` header size on ESP32 by shrinking `count` and
  `capacity` to 16-bit fields while keeping the macOS host layout unchanged.
  In a second step, implement option 2 by turning transient vectors into a
  ringbuffer-backed mutable queue structure with an additional offset/head field.
  The work must be incremental, test-first, and preserve public vector semantics.
todos:
  - id: phase-1-red-layout-and-limit-tests
    content: "RED: Add focused tests for ESP32-only 16-bit vector count/capacity layout, explicit max-capacity guards, and unchanged macOS host behavior"
    status: done
  - id: phase-2-green-esp32-header-shrink
    content: "GREEN: Reduce vector count/capacity to 16-bit on ESP32 only, keep host layout unchanged, and make all allocation/growth code respect the new capacity limit"
    status: done
  - id: phase-3-red-transient-ringbuffer-contract-tests
    content: "RED: Add tests for transient-vector ringbuffer semantics, including FIFO-style front removal, wrap-around, persistent snapshot correctness, and unchanged public vector behavior"
    status: pending
  - id: phase-4-green-transient-ringbuffer
    content: "GREEN: Implement option 2 by adding an offset/head field to transient vectors and making transient mutation paths ringbuffer-backed"
    status: pending
  - id: phase-5-red-event-loop-queue-regressions
    content: "RED: Add or tighten event-loop queue tests proving FIFO order, front-removal correctness, and no behavioral regressions when task queues use transient ringbuffer operations"
    status: pending
  - id: phase-6-green-hot-path-adoption
    content: "GREEN: Update event-loop queue usage to benefit from transient ringbuffer semantics without changing public contracts"
    status: pending
  - id: phase-7-regression-and-budgets
    content: "REFACTOR: Re-run vector, event-loop, heap, and full-suite regressions; verify the ESP32 vector header shrink actually reduces heap usage and that host behavior stays unchanged"
    status: pending
  - id: cleanup
    content: Sourcecode aufräumen – Debug-Code, temporäre Workarounds, tote Codepfade, überflüssige Kommentare und nicht mehr benötigte Hilfsfunktionen entfernen
    status: pending
isProject: false
---

# ESP32 Vector Header Shrink and Transient Ringbuffer

## Goal

Reduce vector header overhead on ESP32 and make transient vectors useful as
efficient queue backings.

The target end state is:

- `CljPersistentVector` uses 16-bit `count` and `capacity` on ESP32 only
- macOS host keeps the current wider layout and behavior
- transient vectors gain a ringbuffer head/offset field and can remove from the
  front without repeated full-array shifting
- public vector behavior remains Clojure-compatible and unchanged for callers

## Current workspace status

Status at the time of this plan update:

- Planning is refined, but the implementation has **not** landed yet in the
  current workspace state.
- `CljPersistentVector` still uses the current wide fields:
  - `unsigned int count`
  - `int capacity`
- `CljTransientVector` still contains only:
  - `CljObject base`
  - `CljPersistentVector *backing`
- `vector_persistent()` still returns the borrowed backing directly; no
  ringbuffer-aware snapshot logic exists yet.
- `event_loop_run_next()` still dequeues via:
  - `vector_nth(task_vec->backing, 0)`
  - `vector_remove_at(task_vec, 0)`
- The current `src/tests/test_vector.c` does **not** yet contain the new Phase 1
  layout/limit tests in the active file content.

Practical consequence:

- No implementation phase can be marked completed yet.
- All execution todos below remain pending until the corresponding code and
  tests are present in the active workspace state and verified.

## Why this change is justified

On ESP32, a vector with more than `65535` slots is already unrealistic because
the pointer array alone would consume about 256 KiB (`65535 * 4` bytes) before
counting the vector header and referenced heap objects. That makes a 16-bit
limit realistic for ESP targets.

Shrinking only `count` would not save header space on ESP32 because padding
would remain. Shrinking both `count` and `capacity` saves real header bytes on
ESP32. The ringbuffer work belongs only to transient vectors so persistent
vector layout and semantics stay simpler.

## Hard constraints

- The change is ESP32-specific for vector header shrinking; macOS host layout
  must stay unchanged unless a later separate decision explicitly changes it.
- Public vector semantics must remain stable.
- No polling loops.
- No compatibility wrappers left behind after migration.
- Optimize for low heap overhead, low object code growth, and low hot-path cost.
- Prefer existing test files/groups instead of adding new ones unless an
  existing group cannot express the contract cleanly.

## Quality gate per step

- Each implementation step starts with failing tests first.
- After every implementation step, run the full unit-test suite
  `./build/unit-tests`.
- Do not continue to the next step until the full suite is green.
- If an ESP32-specific test cannot run in the host suite, add a host-visible
  architecture/layout contract test that still proves the conditional behavior.

## Relevant files and usage patterns

- Vector layout and API:
  - `subjective-c/src/subjective-c/vector.h`
  - `subjective-c/src/vector.c`
- Base object layout:
  - `subjective-c/src/subjective-c/object.h`
- Existing vector tests:
  - `src/tests/test_vector.c`
- Event-loop queue hot path currently using transient vectors with front removal:
  - `src/event_loop.c`
- Nearby queue / runtime regression groups to extend if needed:
  - existing event-loop and timer-related test groups in `src/tests/`

### Direct backing access sites in `event_loop.c` that must migrate

These sites bypass the transient API and access `task_vec->backing` directly.
After Phase 4 they will ignore the ring `head` and produce wrong results unless
updated in Phase 6:

- `vector_nth(task_vec->backing, 0)` — must use `vector_front_transient(task_vec)`
- `vector_clear(task_vec->backing)` — must also reset `head` (or use a new
  `vector_clear_transient(tvec)` helper)
- `vector_count(task_vec->backing)` — safe (count is head-independent), but
  prefer `vector_count(vector_persistent(task_vec))` for consistency

Important current behavior to preserve:

- persistent vectors expose contiguous logical indexing
- `vector_nth`, `vector_assoc`, `vector_conj`, `vector_by_removing_at`,
  `vector_by_inserting_at`, `vector_persistent`, and sequence-facing behavior
  must keep their observable semantics
- transient vectors currently wrap a persistent backing and mutate via helper
  operations such as `vector_push`, `vector_pop`, `vector_remove_at`,
  `vector_insert_at`, and `vector_set_nth_transient`
- the event-loop task queue currently pays for front removal through
  `vector_remove_at(task_vec, 0)`

## Design decisions to lock in

### ESP32-only header shrink

- On ESP32, `CljPersistentVector.count` and `CljPersistentVector.capacity`
  become `uint16_t` fields (unsigned 16-bit, range 0–65535).
- On macOS host, keep the current wider fields (`unsigned int` / `int`) unless
  a separate plan changes that later.
- `make_vector` and all growth/copy paths must reject capacities above the
  supported ESP32 limit clearly and deterministically.
- The static `clj_empty_vector_singleton_data` mirror struct in `vector.c`
  must also use conditional field widths matching `CljPersistentVector`.

### Option 2: transient vectors become ringbuffer-backed

- Add a `uint16_t head` field to `CljTransientVector` (ESP32) or
  `unsigned int head` (host). This field is transient-only; persistent vectors
  never carry a head field.
- Keep the persistent vector payload layout simple.
- Logical indexing for transient operations must map through the head/offset:
  `physical = (head + logical) % capacity`.
- `vector_push` appends at `(head + count) % capacity`; `vector_remove_at(0)`
  increments `head` — both O(1). Arbitrary `insert_at`/`remove_at` at other
  indices remain O(n) with wrap-aware shifting.
- **Invariant: after every backing growth, `head` is reset to 0** (elements are
  copied in logical order into the fresh backing).
- Kein neues API nötig: Callers wie `event_loop.c` greifen direkt auf das
  logische Element 0 zu via `backing->data[head % capacity]`.

#### `vector_persistent` ownership change

- Currently `vector_persistent()` returns a **borrowed** reference to the
  backing (no allocation, no RETAIN).
- After Phase 4, when `head == 0`: behavior stays unchanged (borrowed backing).
- When `head != 0`: must allocate a new persistent vector with elements in
  logical order. The returned vector is **owned** (rc=1). The caller must
  RELEASE or AUTORELEASE it.
- To avoid a split borrowed/owned contract, the simplest safe approach is:
  make `vector_persistent()` always return an **owned** reference
  (`RETAIN(backing)` when `head == 0`, new allocation when `head != 0`).
  Update all callers to RELEASE/AUTORELEASE the result.
- The `as_vector()` inline in `vector.h` calls `vector_persistent()` and
  currently expects a borrowed result — must be updated accordingly.

Front removals in transient hot paths should no longer require repeated
element shifting in the common case.

### Public semantics stay stable

- Public Clojure vector behavior must not change.
- Persistent vectors should still look logically contiguous to all callers.
- Ringbuffer behavior is an internal transient optimization, not a new public
  collection type.

## Test-first phases

### Phase 1 - RED: layout and limit tests

Add failing tests first.

1. Add architecture/layout contract tests proving:
   - ESP32 builds use a smaller vector header than today
   - macOS host layout remains unchanged
2. Add limit tests for ESP32-specific vector creation/growth near `UINT16_MAX`.
3. Add tests that capacities above the ESP32 limit fail clearly instead of
   overflowing or silently truncating.
4. Add tests that existing small-vector behavior is unchanged.

Prefer existing groups:

- extend `src/tests/test_vector.c`
- add architecture/layout assertions to an existing nearby architecture contract
  test group only if needed

Exit criterion:

- the new tests fail against the current implementation because vector fields are
  still wide everywhere and there is no explicit ESP32 cap handling

### Phase 2 - GREEN: ESP32 header shrink

Implement the smallest safe change that makes phase-1 tests pass.

Steps:

1. Introduce conditional field widths (`uint16_t` on ESP32, unchanged on host)
   for `count` and `capacity` in `CljPersistentVector`.
2. Update the `clj_empty_vector_singleton_data` mirror struct in `vector.c` to
   use the same conditional field widths.
3. Update helper signatures/locals only where needed so the code remains compact
   and correct.
4. Add explicit capacity guards in `make_vector` and growth paths.
5. Keep host layout unchanged.
6. Remove any temporary debug scaffolding before leaving the phase.

Exit criterion:

- phase-1 tests turn green
- existing vector semantics remain unchanged
- full suite stays green

### Phase 3 - RED: transient ringbuffer contract tests

Add failing tests before changing transient behavior.

1. `vector_push` followed by repeated front removal preserves FIFO order.
2. Head/offset wrap-around preserves logical indexing.
3. `vector_set_nth_transient` still addresses logical indexes, not raw storage
   slots.
4. `vector_insert_at` and `vector_remove_at` still preserve public logical order.
5. `vector_persistent` exposes the same logical order regardless of internal
   wrap-around.
6. Empty/singleton/small-capacity corner cases behave correctly.

Prefer extending `src/tests/test_vector.c` rather than adding a new test file.

Exit criterion:

- these tests fail until transient vectors support a ringbuffer head/offset model

### Phase 4 - GREEN: transient ringbuffer

Implement option 2.

Steps:

1. Add `head` field to `CljTransientVector` (`uint16_t` on ESP32,
   `unsigned int` on host). Initialize to 0 in `make_vector_transient`.
2. Rework transient operations to use logical-to-physical index mapping:
   `physical = (head + logical) % backing->capacity`.
3. `vector_push`: write at `(head + count) % capacity`, increment count.
   `vector_remove_at(tvec, 0)`: increment `head`, decrement count — O(1).
   Other `insert_at`/`remove_at` indices: O(n) wrap-aware shifting.
4. **Growth invariant**: when backing must grow, copy elements in logical
   order into the new backing and reset `head = 0`.
5. Change `vector_persistent()` ownership contract:
   - `head == 0`: return `RETAIN(backing)` (owned, no copy needed).
   - `head != 0`: allocate new persistent vector in logical order (owned, rc=1).
   - Update all existing callers to RELEASE/AUTORELEASE the result.
   - Update the `as_vector()` inline helper and its callers accordingly.
7. Keep ownership and COW rules compliant with `MEMORY_POLICY.md`.

Constraints:

- do not add a persistent-vector ring header field
- do not change public vector contracts
- avoid extra heap allocations in common transient queue operations (the
  `head == 0` fast path in `vector_persistent()` must remain allocation-free
  apart from the RETAIN)

Exit criterion:

- phase-3 tests turn green
- no public behavior regression is visible to existing vector callers

### Phase 5 - RED: event-loop queue regressions

Add failing tests before adopting the new transient queue behavior in hot paths.

1. Event-loop task queue still executes in strict FIFO order.
2. Repeated front removals do not skip or duplicate tasks.
3. Ingress promotion plus task queue consumption still behaves as before.
4. Queue semantics remain correct across wrap-around / reuse cycles.

Prefer extending nearby existing event-loop / timer / go-block test groups
instead of creating a new broad test suite.

Exit criterion:

- the queue regression tests fail until hot-path callers are adapted to the new
  transient ringbuffer assumptions or helper paths

### Phase 6 - GREEN: hot-path adoption

Adopt the transient ringbuffer where it materially helps.

Primary target:

- event-loop task queue in `src/event_loop.c`

Steps:

1. Replace `vector_nth(task_vec->backing, 0)` with direktem Zugriff auf das
   logische Element 0: `task_vec->backing->data[task_vec->head % capacity]`.
2. `vector_remove_at(task_vec, 0)` — bereits O(1) nach Phase 4, kein
   Code-Change nötig.
3. Replace `vector_clear(task_vec->backing)` mit einem head-bewussten Clear:
   backing leeren **und** `head = 0` zurücksetzen (inline, kein neues API).
4. Audit alle verbleibenden `task_vec->backing` Direktzugriffe und head-aware
   machen wo nötig.
5. Keep task queue semantics unchanged.
6. Keep the code path single and explicit; do not leave old competing queue
   logic behind.
7. Remove dead comments or fallback logic once the new path is proven.

Exit criterion:

- event-loop queue regression tests turn green
- full suite remains green

### Phase 7 - REFACTOR: regression and budgets

After behavior is green:

1. Re-run focused vector tests.
2. Re-run event-loop / timer / go-block related tests touched by the queue path.
3. Re-run heap-sensitive tests that could reflect vector header savings.
4. Run the full unit-test suite.
5. Record the measured before/after vector header size and any observed heap
   delta on ESP32-relevant paths.

Acceptance checks:

- `0 Failures`
- no increased ignored-test count
- host behavior remains unchanged
- ESP32 vector header is smaller
- transient front-removal hot paths no longer depend on repeated full-array
  shifting in the common queue case

## Risks

1. ESP32-only field narrowing could introduce silent overflow bugs.
   Mitigation:
   - explicit limit guards
   - tests near the boundary

2. Ringbuffer indexing could break persistent snapshot order.
   Mitigation:
   - red tests for `vector_persistent` and logical indexing before the change

3. Event-loop queue behavior could regress subtly under wrap-around.
   Mitigation:
   - add targeted FIFO and wrap-around regression tests before adoption

4. Host and ESP32 layouts could drift unintentionally.
   Mitigation:
   - explicit architecture/layout contract tests
   - keep host path unchanged in this plan

## Acceptance criteria

1. ESP32 builds use 16-bit `count` and `capacity` for persistent vectors.
2. macOS host keeps the current wider persistent-vector layout.
3. Vector creation and growth fail clearly when exceeding the ESP32-supported
   capacity range.
4. Transient vectors are ringbuffer-backed and preserve logical vector order.
5. Persistent/public vector semantics stay unchanged.
6. Event-loop queue behavior stays FIFO-correct after adopting transient
   ringbuffer semantics.
7. The full unit-test suite is green after each implementation step and at the
   end.

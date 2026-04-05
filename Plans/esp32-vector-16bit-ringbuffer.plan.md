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
    status: pending
  - id: phase-2-green-esp32-header-shrink
    content: "GREEN: Reduce vector count/capacity to 16-bit on ESP32 only, keep host layout unchanged, and make all allocation/growth code respect the new capacity limit"
    status: pending
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
  become 16-bit fields.
- On macOS host, keep the current wider fields unless a separate plan changes
  that later.
- `make_vector` and all growth/copy paths must reject capacities above the
  supported ESP32 limit clearly and deterministically.

### Option 2: transient vectors become ringbuffer-backed

- Add a transient-only head/offset field.
- Keep the persistent vector payload layout simple.
- Logical indexing for transient operations must map through the head/offset.
- `vector_persistent` must return a persistent snapshot/representation whose
  logical order matches existing behavior.
- Front removals in transient hot paths should no longer require repeated
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

1. Introduce conditional field widths for `count` and `capacity` in
   `CljPersistentVector` for ESP32 only.
2. Update helper signatures/locals only where needed so the code remains compact
   and correct.
3. Add explicit capacity guards in `make_vector` and growth paths.
4. Keep host layout unchanged.
5. Remove any temporary debug scaffolding before leaving the phase.

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

1. Add transient-only ringbuffer state such as `head`/`offset`.
2. Rework transient operations to use logical-to-physical index mapping.
3. Preserve compact fast paths for append/pop when possible.
4. Ensure `vector_persistent` returns a logically ordered persistent result.
5. Keep ownership and COW rules compliant with `MEMORY_POLICY.md`.

Constraints:

- do not add a persistent-vector ring header field
- do not change public vector contracts
- avoid extra heap allocations in common transient queue operations

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

1. Replace front-removal hot paths with ringbuffer-aware transient operations.
2. Keep task queue semantics unchanged.
3. Keep the code path single and explicit; do not leave old competing queue
   logic behind.
4. Remove dead comments or fallback logic once the new path is proven.

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

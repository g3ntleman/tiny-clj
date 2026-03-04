---
name: ""
overview: ""
todos: []
isProject: false
---

# ESP32 Vector Scene Graph Plan (PoC-First, tiny-clj + C)

## Goal

Build a reduced SVG-like 2D graphics engine with a strict PoC-first delivery path:

- Record-first scene nodes (type is implicit by record, no `:type` tag dispatch in game code)
- Group-based scene graph with inherited transforms
- Snapshot-based rendering directly from tiny-clj/subjective-c records (no mandatory patching/canonical C scene cache for PoC)
- Multiple independently updatable scene slots (`game`, `score`, `deco`) with clip rectangles
- Deterministic rendering pipeline validated first on macOS host simulator
- Fixed-first decode/transform path (Q19.13 aligned with `subjective-c`) for deterministic behavior and lower ESP32 CPU load
- Deterministic immediate rendering on SPI displays (ST7789 class) after host PoC is stable
- Thick-stroke primitives suitable for game, menu, and animated vector title screens
- Solid fill color for area primitives (SVG-like paint model MVP)

## Target Architecture: Flat Entity Map + Timeline Animations + C Interpolation

### Scene Model: Flat Entity Map per Slot

Each scene slot (e.g. `game`, `score`, `deco`) is one Atom holding an immutable **flat map**
of `{id → Record}`. Groups reference children by ID, not by embedding. The root entity
has the symbol `root` as its `:id`.

```clojure
(def game-slot (atom
  {root (group {:id root :style style-default :children [3001 3002 3003]})
   3001 (polyline {:id 3001 :t (transform {:tx 0}) :style style-line :pts terrain-pts})
   3002 (tri      {:id 3002 :t (transform {:tx 72 :ty 146}) :style style-player
                   :x1 56 :y1 146 :x2 72 :y2 118 :x3 88 :y3 146})
   3003 (polyline {:id 3003 :t (transform {:tx 200 :ty 126 :rot -90})
                   :style style-rocket
                   :pts (->Timeline [[0 pts-open] [300 pts-mid] [400 pts-closed]] true)})}))
```

Properties:

- **Flat, not nested.** Entities stored by ID in a persistent map. No tree embedding.
- **Groups reference children by ID.** Logical tree arises from `root` → `:children` → ID lookups.
- **Root = symbol `root`.** C starts rendering at `map_get(entities, sym_root)`.
- **Updates are O(1).** `(swap! game-slot assoc-in [3002 :t] new-transform)` – no tree walk.
- **Structural sharing.** Only the changed entity + outer map are re-allocated.
- **`update-nodes` (M8b) no longer needed** for typical updates; direct `assoc-in` replaces tree walking.

### Timeline Animations (declarative, C-evaluated)

Any Record field can hold a `Timeline` instead of a plain value:

```clojure
(defrecord Timeline [keyframes loop])
;; keyframes = [[time-ms value] [time-ms value] ...]

;; Alien cycles through 3 forms with different durations:
(polyline {:id 3003
           :pts   (->Timeline [[0 pts-open] [300 pts-mid] [400 pts-closed]] true)
           :style (->Timeline [[0 style-green] [400 style-red]] true)})
```

C resolves Timelines during traversal – no Clojure eval, no timers:

```c
ID field_val = record_get(entity, FIELD_PTS);
if (is_timeline(field_val)) {
    field_val = timeline_resolve(field_val, current_time_ms);
}
```

Timeline resolution for looping: `phase_ms = time_ms % total_period`, then linear scan
over keyframes (typically 2–5 entries). Cheap enough for the render hot path.

### Three-Layer Separation

```
┌─────────────────────────────────────────────────────────────┐
│  Layer 1: Clojure – Scene State (immutable flat entity map) │
│                                                             │
│  (def game-slot (atom {root (group ...) 3001 (tri ...) ..}))│
│                                                             │
│  • Flat map of {id → Record} per slot, in an Atom           │
│  • State changes via swap!/assoc-in (event-driven)          │
│  • Timeline Records on fields for periodic animations       │
│  • No Clojure eval in the frame loop                        │
└────────────────────────┬────────────────────────────────────┘
                         │ atom_deref (once per frame, no eval)
                         ▼
┌─────────────────────────────────────────────────────────────┐
│  Layer 2: C – Interpolation + Timeline Resolution           │
│                                                             │
│  • Reads flat entity map snapshot from Atom                 │
│  • Resolves Timeline fields by wall clock (modulo arith.)   │
│  • Interpolates transform targets (lerp/easing for smooth   │
│    motion) using mutable C-owned AnimState structs          │
│  • Owns animation timing (dt, framerate-independent)        │
└────────────────────────┬────────────────────────────────────┘
                         │ renders resolved values
                         ▼
┌─────────────────────────────────────────────────────────────┐
│  Layer 3: C – Renderer                                      │
│                                                             │
│  • Traverses logical tree (root → children IDs → lookup)    │
│  • Composes inherited transforms                            │
│  • Rasterizes primitives with resolved field values         │
│  • Read-only on the Clojure snapshot                        │
└─────────────────────────────────────────────────────────────┘
```

### Design Principles

- **Clojure never mutates Records.** State changes produce new immutable Records via `swap!`.
- **C never mutates the Clojure snapshot.** C reads via `atom_deref` (cheap pointer read).
- **Records over Maps** for entities: C knows Record layout (`DEFRECORD`), field access is O(1).
- **No Clojure eval per frame.** Clojure sets state event-driven (collision, timer, input).
  C resolves Timelines and interpolates every frame with fixed time budget.
- **Interruption-safe.** New target mid-animation → C interpolates from current visual
  position toward new target (no jump, no restart).
- **Timelines are data, not code.** Periodic animations (form cycling, blinking, color pulsing)
  are Timeline Records on fields – C evaluates them with pure arithmetic, no timers needed.
- **Animation descriptors (TODO).** Declarative transition animations (easing, duration, from/to
  for smooth motion) should also execute in C. Exact contract to be designed after flat entity
  map and Timeline are stable (see Optional Extension A for initial sketch).

## Constraints

- MCU class: ESP32 (resource-constrained embedded target)
- Display class: ST7789 320x240 over SPI
- macOS host simulator is mandatory before device bring-up
- Shared render core between simulator and ESP32 (avoid logic forks)
- Host emulation must happen at driver/backend level (not above primitive rasterization),
so primitive generation/rasterization bugs are debuggable on host.
- No required full framebuffer on device
- tiny-clj should not do per-pixel rendering work
- Engine must stay compact and record-friendly
- Decode + transform hot paths should prefer fixed-point (`CLJ_FIXED_FRAC_BITS`) and only quantize to integer at raster boundaries.
- Render-thread safety rule:
  - the native C render thread is read-only on published scene snapshots
  - no MEMORY macros (`RETAIN`, `RELEASE`, `AUTORELEASE`, `ASSIGN`) in render-thread hot path
  - no autorelease pool setup/usage in render thread
  - memory ownership changes/allocation happen only in producer/update thread before atomic snapshot publish
  - no blocking/channel operations in render hot path; use bounded lock-free queues for control/completion handoff
- Backend-parity rule (host <-> ESP32):
  - define one shared backend submission interface (e.g. `begin_frame`, `submit_rect`, `end_frame`)
  - render thread owns backend submission on both targets
  - macOS main/UI thread is presentation-only (`mfb_update_ex`), matching ESP32 where render thread drives SPI writes

## Definition Of Done (project-level)

1. macOS simulator renders deterministic reference frames from the same scene contract used on ESP32.
2. Nested groups with inherited transforms render deterministically frame-to-frame.
3. Required primitives work: `Group`, `Line`, `Polyline`, `Rect`, `Tri`, `VText`.
4. Thick stroke (`width >= 1`) looks stable and readable on 320x240.
5. Solid fill color works for area primitives (`Rect`, `Tri`, `Polyline` with `closed=true`) and respects slot clipping.
6. World movement is achievable by updating group transform fields only (e.g. `world.t.tx`).
7. Multiple scene slots with `clip-rect` can be rendered independently and unchanged slots are skipped.
8. SPI backend supports slot-scoped clipping + window/burst writes without requiring a full-screen redraw.
9. Optional-later delta paths (id-based patches and/or pointer-identity subtree reuse) are compatible with the same render core.

## Delivery Strategy (Stepwise)

Phase A (PoC on host):

- Build and validate scene model, transforms, rasterization, thick lines, and snapshot-slot render flow on macOS.
- Use deterministic frame dumps/checksums as regression gates.

Phase B (embedded integration):

- Reuse validated render core on ESP32 with SPI transport integration.
- Tune clipping/write strategy for SPI bandwidth limits.

## Milestone 0: Scene Contract + Record Schema

Status: DONE (`DEFRECORD` path active; header-serialization path removed)

Tasks:

- Define mandatory node records:
  - `Group`
  - `Line`
  - `Polyline`
  - `Rect`
  - `Tri`
  - `VText`
- Define optional-later node records:
  - `Circle`
  - `Path` (initially Move/Line only)
- Define shared field convention for all node records:
  - `id` (stable key)
  - `t` (optional transform; nil means identity)
  - `style` (at least stroke color + width)
  - `visible` (optional bool)
- Define default values and validation rules.
- Define nil/optional decoding contract (record-friendly direct render path):
  - any field value may be `nil`
  - C decoders must apply typed defaults (`0`, `1`, `true`, etc.) and inheritance semantics where applicable
  - `nil` and explicit `false` must remain semantically distinct
- Define slot-index cache invariant for direct record rendering:
  - slot indices for required record fields are precomputed during schema setup and treated as always known
  - enforce this with `CLJ_ASSERT(...)` during schema initialization / validation
  - optionality applies to slot values (`nil`), not to slot-index discovery in the render hot path
- Define direct-render record contract for C (slot-indexed field access; no keyword lookup in render hot path).
- Decision (layout consistency between Clojure and C):
  - `tiny-gfx.scene` and C `DEFRECORD` declarations are the single source of truth for record field order/layout.
  - C code uses `DEFRECORD` typed overlays plus runtime descriptor-index cache populated in `tiny_gfx_ensure_schema(...)`.
  - Build-time header generation/codegen has been removed from the active workflow.
  - Keep runtime checks minimal (descriptor presence/type), avoid large handwritten index-preparation/validation blocks in hot paths.
- Current implementation notes:
  - `tiny-gfx.scene` exists and registers the record descriptors used by the C renderer.
  - Runtime schema lookup/caching from C is implemented (descriptor-driven field indices).
  - C-side layout-compatible overlays are declared via `DEFRECORD(...)` in `tiny_gfx.h`.
  - Host-only tooling namespace `tiny-gfx.converter` under `libs/tiny-gfx/converter.clj` remains optional helper tooling (e.g. SVG subset conversion via `group-from-svg`), without any header-serialization helpers.
  - Slot-index contract enforcement stays in C via runtime descriptor lookup + `CLJ_ASSERT(...)` (no generated index tables).
- Keep canonicalization/compiled C scene cache optional for later optimization.

Done when:

- Record schema and direct-render access contract are documented and testable.
- Nil/default/inheritance decoding behavior is documented and covered by schema-level tests.
- No active header-serialization path remains in runtime/build/documented workflow.

## Milestone 1: macOS Host Simulator Skeleton (PoC Gate 1)

Status: DONE

Tasks:

- Add a host-side simulator target for 320x240 rendering.
- Implement offscreen RGB565 frame target in host memory.
- Implement host simulator as a driver/backend implementation under the same primitive/render API
used by ESP32 (do not bypass primitive rasterizers with a separate host-only drawing path).
- Add deterministic frame export (PPM or raw dump) for golden testing.
- Add frame checksum utility to compare expected vs actual output.
- Ensure simulator and embedded paths share the same render core and record-slot scene contract.

Done when:

- A simple static scene renders on macOS and produces stable frame checksums across runs.

## Milestone 2: Transform System (SVG-like behavior)

Status: DONE

Tasks:

- Implement `Transform` record semantics:
  - `tx`, `ty` (translation)
  - `sx`, `sy` (scale)
  - `rot` (rotation)
- Implement identity defaults:
  - `tx=0`, `ty=0`, `sx=1`, `sy=1`, `rot=0`
- Implement scene traversal rule:
  - `render(node, parentT)`
  - `T = compose(parentT, node.t)` (`node.t=nil` => `parentT`)
- Use affine 2D matrix internally for composition (recommended).
- Fix operation order convention:
  - Scale -> Rotate -> Translate

Done when:

- Nested group transforms match expected world-space coordinates in tests.

## Milestone 3: Primitive Rasterizers (Baseline Stroke = 1)

Status: DONE

Tasks:

- Implement primitive geometry fields and draw path:
  - `Line`: `x1 y1 x2 y2`
  - `Polyline`: `pts` vector (`[[x y] ...]`), optional `closed?`
  - `Rect`: `x y w h`
  - `Tri`: `x1 y1 x2 y2 x3 y3`
  - `VText`: `x y scale rot text`
- Implement baseline stroke rendering (`width=1`).
- Add viewport clipping for all primitives.
- Ensure deterministic integer pixel placement rules.

Done when:

- A mixed test scene with all required primitives renders correctly with `width=1` in simulator mode.

## Milestone 4: Thick Line Engine (Required)

Status: DONE for now (functional and used by host demo; explicit cap/join semantics + perf gates deferred)

Tasks:

- Add `stroke_width` in pixels (integer, `>=1`).
- Implement fast path for axis-aligned lines:
  - draw as filled rectangles
- Implement general thick line strategy:
  - initial choice: multi-parallel-lines (simple) or quad extrusion (preferred geometry)
- Apply thick stroke handling to:
  - `Line`, `Polyline`, `Rect` edges, `Tri` edges, `VText` glyph strokes
- Define initial cap/join behavior:
  - caps: `:butt` default
  - joins: bevel/none default
- Keep advanced caps/joins optional for later (deferred):
  - `:square`, `:round`, miter/round joins
- Perf/frame-time budget validation deferred.

Done when:

- Thick lines are visually stable in simulator output and within host frame-time budget.

## Milestone 4b: Solid Fill Color (SVG-like MVP)

Status: DONE (implementation + regression tests)

Tasks:

- Extend style contract with fill-color-only MVP fields:
  - `has_fill` (bool)
  - `fill_rgb565` (u16)
- Keep MVP scope intentionally small:
  - no `fill-rule` yet
  - no fill opacity/alpha blending
  - no gradients/patterns
- Implement integer/fixed-point friendly fill rasterization for:
  - `Rect`
  - `Tri`
  - `Polyline` when `closed=true`
- Apply SVG-like paint order per primitive:
  - fill first, stroke second
- Ensure fill path respects active clip rectangle/slot clipping and deterministic pixel rules.
- Add host regression tests for:
  - filled rect/tri/closed-polyline basics
  - clipping behavior
  - deterministic output across runs

Done when:

- Filled area primitives render with stable solid `fill_rgb565` on host and are clipping-correct.
- Existing stroke behavior remains unchanged for primitives without `has_fill=true`.

## Milestone 5: Snapshot Slot Update Path (PoC Gate 2, Patch Optional)

Status: DONE for host PoC (slot-change wait API + atom-bound changed-slot host rendering + dedicated render-thread split implemented)

Tasks:

- Define `FrameScene`/render-slot record contract:
  - `root`
  - `clip-rect` (single unified rect for erase + render clipping)
  - `z`
  - `visible?`
  - `opaque?` / clear policy (`erase-rgb565` optional; no separate `erase-rect`)
  - optional guard pixels for conservative clipping
- Define snapshot publication protocol for render thread consumption:
  - atomically replace one `FrameScene` descriptor (or equivalent) without in-place mutation of the active snapshot
  - enforce thread contract: render thread only reads immutable records; producer thread owns all retain/release work
- Provide a blocking C API for atom/snapshot changes:
  - render thread can wait (blocking) until any registered scene atom/snapshot generation changes
  - API returns changed-slot bitmask/list plus new generation values
  - wakeup primitive should be condition/event based (no busy polling loop)
  - optional timeout variant is allowed as safety fallback
- Render multiple explicit scene roots/atoms in host runtime (e.g. `game`, `score`, `deco`).
- In render thread, track previous slot snapshot and only clear/render/send slot region when the slot snapshot or slot properties change.
- Render thread wait strategy:
  - block/sleep until any scene slot snapshot changes (event/condition based wakeup)
  - avoid continuous busy rendering when no slot changed
  - rationale: lower power draw, better battery life, and reduced thermal load
  - implementation note: host path uses `VgSlotChangeTracker` with a dedicated render thread that blocks on slot-change wakeups
- Render only the affected slot region (`clip-rect`), not the full framebuffer.
- If a slot moves, treat dirty region as `union(old_clip_rect, new_clip_rect)`.
- Validate non-overlapping slot convention with conservative bounds (stroke width / text fringe guard).
- Validate scrolling/parallax using separate scene slots or group transforms within a slot.
- Current state (optional support path): `vg_scene_apply_patch()` exists for single `id`-based patch application.
- Current implementation notes (this milestone):
  - Blocking slot-change API exists in C via `VgSlotChangeTracker` (`scene.h/.c`):
    - per-slot generation counters
    - publish API (`vg_slot_change_tracker_publish`)
    - blocking wait API returning changed-slot bitmask + latest generations (`vg_slot_change_tracker_wait_for_changes`)
  - Host viewer uses explicit scene-slot atoms (`deco`, `score`, `game`) as snapshot carriers:
    - slot publications write immutable `FrameScene` snapshots via `atom_reset`
    - dedicated render thread blocks for tracker changes and renders only changed slots
    - main/UI thread only presents framebuffer output and does not execute slot rasterization
    - unchanged slots are skipped end-to-end in the host render path
  - Unit tests cover tracker behavior:
    - changed-mask/generation reporting
    - timeout/no-change behavior
- Optional later explicit patch path (not required for PoC):
  - define minimal patch operations (`transform`, `text`, `visibility`, `style`)
  - batch apply, guardrails, overflow behavior
- Optional later optimization path (not required for PoC):
  - for persistent tiny-clj/subjective-c render records, use pointer-identity diffing
  (`old_subtree_ptr == new_subtree_ptr`) to skip unchanged subtrees / reuse cached decode
  - treat this as a complementary delta strategy to explicit patch vectors, not a replacement mandate

Done when:

- Multiple scene slots render correctly on host, and unchanged slots are skipped without visual artifacts.

## Milestone 6: VText (Vector/Stroke Font)

Status: DONE (PoC/host path)

Tasks:

- Define minimal stroke-font glyph representation.
- Implement `VText` draw path using line/polyline stroking.
- Support `scale`, `rot`, and inherited parent transforms.
- Support style-based stroke color and width.
- Validate animated/rotated title text ("flying text") use case in simulator.
- Current state: arcadefont-based glyphs are integrated with host-viewer visual regression coverage.

Done when:

- Animated vector text remains readable and stable under transform updates.

## Milestone 7: ESP32 SPI Backend Integration (Post-PoC)

Status: TODO

Tasks:

- Integrate validated raster output with SPI transport model:
  - set window + burst writes
- Introduce/consume shared backend submission interface used by both host and ESP32:
  - backend command surface (frame begin/end + dirty-rect submission)
  - keep renderer/backend boundary identical across targets
  - no host-only rendering branch above this interface
- Map each render slot `clip-rect` to SPI address windows (one window per dirty slot in the simple path).
- Keep backend boundary identical between host and ESP32 so host can emulate the device driver layer
while still exercising the same primitive rasterization pipeline.
- Keep full framebuffer optional (not mandatory).
- Implement minimal clipping/culling before draw submission.
- Add slot-dirty update mode:
  - only clear/render/send changed slots
  - prefer a small number of larger slot windows over many tiny widget windows
- Define per-slot background policy for no-readback displays:
  - opaque slots can overwrite fully
  - non-opaque slots require explicit clear of dirty region
- Account for slot movement:
  - dirty window = `union(old_rect, new_rect)` to avoid trails
- Add optional dirty-region mode switch (sub-slot refinement later).
- Add backend conformance checks:
  - same scene input should produce same primitive command sequence as simulator.
- Define host-analogue execution model:
  - render thread performs backend submission stage on macOS too
  - UI thread only presents completed front buffer/frame
  - host backend path follows the same dirty-rect submission contract as ESP32 SPI path

Proposed backend interface sketch (implementation target for this milestone):

```c
typedef struct {
    int16_t x;
    int16_t y;
    int16_t w;
    int16_t h;
} VgBackendRect;

typedef struct {
    /* Called once per output frame before dirty rect submissions. */
    bool (*begin_frame)(void *ctx, uint32_t frame_id);
    /* Submit one dirty rect in framebuffer coordinates. */
    bool (*submit_rect)(void *ctx,
                        VgBackendRect rect,
                        const uint16_t *rgb565_pixels,
                        uint16_t stride_px);
    /* Called after all rect submissions for this frame. */
    bool (*end_frame)(void *ctx, uint32_t frame_id);
} VgBackendOps;
```

Execution contract for this interface:

- `submit_rect` receives clipped dirty regions only; no backend-side scene traversal.
- Render thread owns all `VgBackendOps` calls on both targets.
- macOS backend implementation:
  - copies submitted RGB565 dirty regions into host present buffer (or staging buffer)
  - UI/main thread performs presentation only (`mfb_update_ex`) and does not call render APIs
- ESP32 backend implementation:
  - maps each submitted rect to SPI window + burst write sequence
  - no full-frame requirement in the default path
- Determinism/conformance:
  - for identical scene snapshots, host and ESP32 must observe equivalent rect submission order/data contract
  - backend conformance tests compare emitted rect sequences for representative scenes

Done when:

- Scene renders on device without full-frame requirement and with pacing consistent with simulator logic.
- Shared backend submission interface is used on both targets, and conformance checks pass for representative scenes.

## Milestone 8: Fixed-First Decode + Transform Path (Required for ESP32)

Status: DONE (core decode/transform/render pipeline is fixed-first; host documentation + micro benchmark captured; ESP32 build path validated; side-by-side device perf capture still optional/pending)

Motivation:

- Primary target is deterministic output with lower CPU load on ESP32.
- `subjective-c` already provides fixed numeric representation (`CLJ_FIXED_FRAC_BITS`), so decode and transform should stay fixed as long as possible.
- Integer conversion should happen only where raster APIs require pixel-space ints.

Tasks:

1. **Canonical numeric decode policy (`scene.c` decoder path):**
  - Prefer fixed/raw decode for transform-relevant fields (`sx`, `sy`, text scale, composed transform internals).
  - Keep `fixnum` + `fixed` accepted on input records; avoid float conversion in hot decode paths.
  - Keep typed defaults deterministic (`nil` => stable defaults).
2. **Boundary-based quantization:**
  - Keep world/local transform math in fixed-point until raster boundary.
  - Convert to integer only at explicit raster boundaries (`gfx_draw_`*, clip/window setup, framebuffer index math).
  - Avoid repeated fixed->int->fixed ping-pong across compose/apply stages.
3. **Hot-path cleanup for ESP32 CPU:**
  - Remove/avoid float-based helper usage in decode/transform hot path.
  - Keep non-cardinal/trig fallback paths isolated and cold.
  - Ensure branch structure favors common integer/fixed cases.
4. **Contract + assertions:**
  - Document fixed-first contract for scene records (`Transform`, `VText`, slot clip fields).
  - Keep `CLJ_ASSERT` guards for typed overlay layout assumptions in debug builds.
  - No release-mode guard overhead on hot paths.
5. **Regression + determinism gates:**
  - Scene graph test suite must pass with unchanged or intentionally updated golden checksums.
  - Add/keep tests covering mixed numeric inputs (`fixnum` + `fixed`) and nil defaults.
  - Verify host and ESP32 paths keep identical decode semantics.
6. **Performance validation:**
  - Add a small benchmark/profiling slice for decode+render on representative scene sizes.
  - Compare before/after CPU usage on host and (where possible) ESP32.

Implementation notes (2026-03-04):
- Fixed-first contract + quantization boundaries documented in `docs/VECTOR_SCENE_FIXED_FIRST.md`.
- Host decode+render micro benchmark added in `test_vector_scene_graph_decode_render_host_micro_benchmark`.
- Host sample timings recorded for `deco`, `score`, and `game` frame scenes (Debug build).
- Cross-target runtime benchmark function added: `tiny-clj.runtime/vector-scene-bench` (host + ESP32 UART REPL).
- ESP32 IDF component source lists were aligned with host build sources for this path (`subjective-c/record.c`, `gfx.c`, `vector_scene_graph.c`, `scene.c`, `tiny_gfx.c`, `lockfree_spsc_queue.c`), and `./build_idf.sh --no-move` now completes successfully.
- ESP32 side-by-side CPU comparison remains an optional follow-up, pending device run/capture on hardware.

Done when:

- Decode + transform hot paths are fixed-first by default, with integer conversion only at raster boundaries.
- Deterministic frame outputs stay stable across runs.
- CPU cost in representative ESP32 scenes is reduced or at least non-regressed.

## Milestone 8b: Batched Scene Update API (Clojure-Side)

Status: DONE

Motivation:

- Game scenes have 50+ sprites and 10+ groups; per-entity tree walks don't scale.
- A single batched walk with N lookups is O(nodes) instead of O(N × nodes).
- This is the Clojure-side primitive that both timer-based and future core.async-based animation patterns build on.

Tasks:

1. `**update-nodes` function in `tiny-gfx.scene`:**
  - Takes a root node and a map `{:id update-fn ...}`.
  - Single recursive walk over the tree.
  - For each node with `:id` in the map: apply `update-fn`, remove `:id` from map (`dissoc`).
  - Early exit: when map is empty (`(empty? updates)`), return remaining subtree unchanged (no recursion).
  - Works with plain maps and records alike (uses `:id` and `:children` keywords).
2. **Unit tests:**
  - Flat children: 2 of 3 nodes updated, 1 unchanged.
  - Empty updates map: node returned unchanged.
  - Nested groups: updates at different tree depths.
3. **Integration with game-state pattern:**
  - Designed for `(swap! game-scene update-nodes {...})` with `schedule-periodic` timers.
  - Compatible with future core.async go-block orchestration (same primitive, different scheduling).

Complexity characteristics:

- 1 walk per frame (not N walks for N changes).
- O(nodes) visits worst case, often less due to early exit after all changes applied.
- O(1) map lookup per visited node (change-set is a persistent map).
- Allokations: only changed nodes + path to root (structural sharing for unchanged subtrees).

Done when:

- `tiny-gfx.scene/update-nodes` is available and tested.
- Game code can batch all per-frame entity updates into a single tree walk.

## Milestone 9: Flat Entity Map + Timeline + Declarative Scene Architecture

Status: IN PROGRESS (`9a` + `9b` + `9c` baseline DONE on host/ESP32 build path; `9d` AnimState + remaining host-viewer ASSIGN removals still TODO)

Target: Migrate host-viewer to the flat-entity-map architecture (see "Target Architecture"):

- Each slot = one Atom holding `{id → Record}` (flat, not nested)
- Root entity has `:id root` (symbol)
- Groups reference children by ID
- Timeline Records on fields for periodic animations (C-evaluated, no timers)
- C reads snapshot via `atom_deref`, resolves Timelines, interpolates, renders

Current gap:

- Flat entity maps and Timeline decode are now in place, but host-viewer still mutates several gameplay records directly from C (`ASSIGN(...)`).
- Smooth event-driven interpolation state (`AnimState`) is not implemented yet.
- Full elimination of direct C-authored per-frame target writes is still pending.

### 9a: Flat Entity Map (Clojure side)

- Restructure scene from nested tree to flat `{id → Record}` map per slot.
- Root entity uses symbol `root` as `:id`. C starts rendering at `root`.
- Groups list children as ID vectors: `(group {:id root :children [3001 3002 3003]})`.
- Updates via `(swap! slot assoc-in [id :field] new-value)` – O(1), no tree walk.
- `update-nodes` (M8b) becomes optional convenience; direct `assoc-in` is the primary API.
- Implementation note (2026-03-04):
  - `tiny-gfx.host-viewer-demo/create-demo-bundle` now publishes slot roots as flat `{id -> Record}` maps.
  - Root entity key/id is symbol `root`; groups reference children by ID vectors (not embedded records).

Example:
```clojure
(def game-slot (atom
  {root (group {:id root :children [3001 3002 3003]})
   3001 (polyline {:id 3001 :t (transform {:tx 0}) :style style-line :pts terrain-pts})
   3002 (tri      {:id 3002 :t (transform {:tx 72 :ty 146}) :style style-player
                   :x1 56 :y1 146 :x2 72 :y2 118 :x3 88 :y3 146})
   3003 (polyline {:id 3003 :t (transform {:tx 200 :ty 126 :rot -90})
                   :style style-rocket :pts rocket-pts})}))
```

### 9b: Timeline Record for periodic animations

- Define `(defrecord Timeline [keyframes loop])`.
  - `keyframes` = vector of `[time-ms value]` pairs, per-keyframe durations (unequal allowed).
  - `loop` = boolean (true → `time_ms % total_period`; false → clamp at last keyframe).
- Any entity field can hold a Timeline instead of a plain value.
- C resolves during traversal: check if field is Timeline, resolve by wall clock.
- No Clojure eval, no timers needed for periodic animations.
- Implementation notes (2026-03-04):
  - `tiny-gfx.scene` now defines `Timeline` (`[keyframes loop]`).
  - `scene.c` resolves Timeline fields during traversal.
  - Numeric keyframes interpolate linearly (Q19.13), including looped phase wrapping.
  - Transform keyframes interpolate `tx/ty/sx/sy/rot` and compose via `vg_transform_fixed_from_transform`.
  - Deterministic render entry points added for tests: `vg_render_scene_record_at_ms` and `vg_render_scene_record_clipped_at_ms`.

Example (alien cycling 3 forms with different durations):
```clojure
{3003 (polyline {:id 3003
                 :pts   (->Timeline [[0 pts-open] [300 pts-mid] [400 pts-closed]] true)
                 :style (->Timeline [[0 style-green] [400 style-red]] true)})}
;; C resolves: phase_ms = time_ms % 600; scan keyframes to find active frame
```

### 9c: C Renderer for flat entity map

- C reads the flat map snapshot from Atom via `atom_deref`.
- Traversal: lookup `root` → read `:children` → lookup each child ID → recurse.
- Transform inheritance: compose parent transform with each child's `:t` field.
- Timeline resolution: at each field read, check for Timeline Record, resolve by time.
- Entity map lookup by ID must be efficient (persistent map `get` or C-side index cache).
- Implementation note (2026-03-04):
  - `scene.c` render traversal now supports both legacy embedded-tree scenes and flat-map scenes.
  - If `FrameScene.root`/`Scene.root` is a map, renderer resolves `root` symbol entry and traverses group child IDs via `map_get`.
  - Timeline resolution is integrated in primitive/style/transform field reads during record traversal.

```c
void render_entity(ID entity_map, ID id, Transform parent_t, uint32_t time_ms) {
    ID entity = map_get(entity_map, id);
    ID raw_t = record_get(entity, FIELD_T);
    ID resolved_t = is_timeline(raw_t) ? timeline_resolve(raw_t, time_ms) : raw_t;
    Transform t = compose(parent_t, decode_transform(resolved_t));

    if (is_group(entity)) {
        ID children = record_get(entity, FIELD_CHILDREN);
        for (int i = 0; i < vec_count(children); i++) {
            render_entity(entity_map, vec_nth(children, i), t, time_ms);
        }
    } else {
        render_primitive(entity, t, time_ms);  // resolves Timeline fields
    }
}
// Entry: render_entity(entity_map, sym_root, identity, now_ms);
```

### 9d: C Interpolation State for smooth motion (AnimState)

- For transform targets that change event-driven (e.g. player jump, rocket move),
  C maintains mutable `AnimState` structs to interpolate smoothly.
- Per frame: read target transform from entity map, lerp `current → target`.
- Interruption-safe: new target mid-animation → interpolate from current visual position.
- AnimState is separate from the Clojure snapshot (C-owned, mutable).
- Timeline (periodic, data-driven) and AnimState (smooth motion, C-driven) are complementary:
  Timeline = cyclic field replacement; AnimState = continuous interpolation toward targets.

### 9e: Animation descriptors (TODO – to be clarified)

- Declarative transition animations (easing, duration, from/to for smooth motion)
  must execute in C, not the interpreter.
- Open questions:
  - Are animation descriptors authored in Clojure (as Records) and consumed by C?
  - How do they interact with Timeline fields?
    (Timeline = periodic/cyclic; animation descriptor = one-shot/transition?)
  - Completion callbacks back to scheduler (reuse collision callback ingress?)
- See Optional Extension A for initial animation record sketch.
- Decision deferred until 9a–9d are stable.

### 9f: Migration of host-viewer demo

- Restructure `tiny-gfx.host-viewer-demo` from nested `create-demo-bundle` to flat entity maps.
- Replace direct `ASSIGN(transform->tx, ...)` calls in C with the
  `atom_deref → resolve Timelines → interpolate → render` pipeline.
- Move target-setting logic to Clojure (event-driven) where possible.
- Keep C-owned: frame pacing, backend submission, metrics, presentation.
- Implementation notes (2026-03-04):
  - Demo includes first periodic Timeline slice in production path: moving `hbar` now uses a looping transform Timeline from Clojure data.
  - Host viewer no longer updates `hbar` via C-side transform `ASSIGN`.
  - Remaining gameplay transform/geometry writes (`terrain/player/obstacle`) are still C-driven and tracked as follow-up.

### 9g: Validate scene reuse + scheduler integration

- Gameplay HUD, menu UI, vector title animation.
- Scene updates via `(swap! slot assoc-in [id :field] new-value)`.
- Timer/event-driven via existing tiny-clj scheduler.
- Publish only changed slot snapshots to keep render wakeups sparse.

### 9h: Host-viewer color authoring readability (DONE)

- Status: DONE (2026-03-04).
- `tiny-gfx.scene/color` converts `0xRRGGBB` → RGB565.
- `tiny-gfx.scene/web-hex->color` converts `"#RRGGBB"` → RGB565.
- Demo palette migrated to `(color 0xRRGGBB)` style.

### Done when

- Host-viewer demo uses flat entity map architecture end-to-end:
  Clojure `{id → Record}` in Atom → C resolves Timelines + interpolates → renders.
- Root entity is identified by symbol `root`.
- Periodic animations run via Timeline Records (no Clojure timers for visual cycling).
- No direct `ASSIGN` of computed positions from C into scene Records.
- One vertical slice runs the same scene model in simulator and on device.

## Milestone 10: Collision Contract + C-Driven Callback

Status: TODO

Collision spec (first-class contract):

- Collision rules are declared in Clojure and published with the scene snapshot.
- Proposed rule shape (record or map-like equivalent):
  - `:id` stable rule id (for debug/traceability)
  - `:slot` optional slot selector (`:game`, `:score`, `:deco`), default `:game`
  - `:a-id` stable object id
  - `:b-id` stable object id
  - `:phase-mask` set of phases to emit (`#{:enter :stay :exit}`), default `#{:enter :exit}`
  - `:enabled` bool, default true
  - optional `:cooldown-ms` (minimum delay between repeated `:stay` callbacks)
- Determinism rules:
  - pair identity is canonicalized in C (`min(id), max(id)`) for stable latch keys
  - unchanged overlap state must not re-emit `:enter`
  - disabling/removing a rule clears its latch state without synthetic events

Collision callback event contract (C -> scheduler):

- Event payload includes at least:
  - `:rule-id`
  - `:slot`
  - `:a-id`
  - `:b-id`
  - `:phase` (`:enter`/`:stay`/`:exit`)
  - `:snapshot-gen` (or equivalent monotonically increasing generation)
  - `:ts-ms` host/device monotonic timestamp used by C
- Ordering rules:
  - events are emitted in deterministic rule iteration order per processed snapshot
  - scheduler consumption order must match enqueue order (FIFO)

Thread-safety integration boundary (required for scheduler callbacks):

- Keep scheduler logic single-threaded; only ingress is thread-safe.
- C render/collision thread must never mutate scheduler internals directly.
- Introduce thread-safe callback ingress queue (MPSC -> scheduler consumer):
  - producer: C render/collision thread enqueues callback events
  - consumer: scheduler thread drains queue at safe points
  - wakeup: condition/event signal (no busy polling)
  - shutdown: explicit close/drain semantics to avoid lost events
- Memory/ownership rule for ingress payloads:
  - producer writes immutable payload copies
  - consumer owns processing + cleanup on scheduler thread

Stepwise delivery plan (implementation order):

1. Collision contract freeze (Clojure side, no runtime behavior change yet)
  - finalize collision-rule schema fields and defaults
  - finalize callback-event payload fields and phase vocabulary
  - document canonical pair-id behavior and latch semantics
  - add schema-level tests for rule decoding/default handling
2. Scheduler callback ingress (thread-safe boundary only)
  - implement thread-safe ingress queue (MPSC + FIFO)
  - add wakeup signaling and scheduler-thread drain API
  - add close/drain shutdown semantics
  - tests: FIFO ordering, concurrent producer safety, scheduler-thread-only handler execution
3. C collision engine baseline (without callback wiring enabled by default)
  - decode collision rules from snapshot
  - resolve object ids to render objects and compute overlap
  - maintain enter/stay/exit latch state per canonical pair key
  - tests: deterministic overlap transitions from synthetic snapshots
4. Bridge C collision events into scheduler ingress
  - enqueue callback payloads from C collision pass
  - drain and dispatch callback handlers on scheduler thread
  - tests: deterministic event order, no duplicate `:enter` on unchanged overlap
5. Slot/scenario integration and rollout in host viewer
  - enable collision rules first for `:game` slot
  - keep manual hitbox override path until auto-bounds contract is fully validated
  - add feature flag for safe rollback during integration
  - tests: host demo scenario coverage (`enter`/`exit`, moving obstacle, stable-id updates)
6. Hardening and performance pass
  - bounded queue/backpressure policy for event bursts
  - watchdog/metrics hooks for dropped/queued callback counts
  - soak tests with long-running host scene updates
  - prepare ESP32 follow-up checklist (same contract, backend-specific limits)

Execution gates per step (must pass before next step):

- Gate 1 (after step 1):
  - collision rule schema + defaults are fixed and documented
  - callback payload schema is fixed and versioned in docs/tests
  - no runtime behavior change in renderer/scheduler yet
- Gate 2 (after step 2):
  - scheduler ingress queue API is available and thread-safe
  - callback handlers still run exclusively on scheduler thread
  - queue close/drain is covered by unit tests
- Gate 3 (after step 3):
  - C collision evaluation produces deterministic phase transitions from synthetic snapshots
  - latch state behavior is stable across unchanged frames and rule removal
  - callback bridge still disabled by default (feature-gated)
- Gate 4 (after step 4):
  - C collision events are enqueued and dispatched via scheduler ingress
  - event ordering remains deterministic (rule order -> FIFO drain order)
  - no duplicate `:enter` events on unchanged overlap
- Gate 5 (after step 5):
  - host viewer integrates `:game` collision rules end-to-end under feature flag
  - fallback path (manual hitbox behavior) remains available
  - demo scenarios validate `:enter`/`:exit` semantics with stable ids
- Gate 6 (after step 6):
  - burst behavior/backpressure policy is validated
  - queue/dispatch metrics are available for diagnostics
  - soak runs complete without callback loss, deadlock, or scheduler thread violations

Immediate next implementation slice:

- Start with Step 1 only (contract freeze):
  - add explicit rule/event schema docs in code comments and plan references
  - add schema-focused tests (defaults, disabled rules, phase-mask normalization)
  - postpone all scheduler/C runtime changes until Gate 1 passes

Tasks:

- Define collision-spec records in tiny-clj with stable `:id` references:
  - each collision rule explicitly lists `:a-id` and `:b-id` (stable object ids, no transient pointers)
  - ids must be stable across snapshot updates to keep collision routing deterministic
- Define how collision checks are attached to a scene/slot snapshot:
  - collision rule vector is part of the published scene snapshot contract
  - tiny-clj is the source of truth for which collision pairs are checked
  - missing/unknown ids are ignored safely (no hard crash in render/update loop)
- Implement collision evaluation in C as an automatic step for each relevant snapshot update:
  - C resolves configured ids to objects, executes overlap tests, and detects state changes (enter/stay/exit)
  - no per-pixel logic in tiny-clj; checks use decoded primitive bounds/shape semantics
- Emit collision events back into the tiny-clj scheduler via callback bridge:
  - callback payload includes at least `:a-id`, `:b-id`, `:phase` (`:enter`/`:stay`/`:exit`) and optional frame/slot metadata
  - callback delivery is deterministic and ordered per scheduler callback dispatch
- Implement scheduler ingress thread-safety for collision callbacks:
  - add thread-safe callback queue + wakeup signaling
  - ensure scheduler-thread-only processing of queued callbacks
  - add queue close/drain behavior for clean shutdown
- Add host-side tests for collision determinism and callback behavior:
  - stable ids across snapshots
  - moved object triggers enter/exit transitions correctly
  - unchanged frame does not duplicate `:enter` events
  - enqueue/drain order remains FIFO under concurrent producer activity
  - scheduler thread alone executes callback handlers (no cross-thread handler execution)
- Current host-demo state (intermediate step):
  - obstacle world bounding box is already derived automatically from geometry + explicit transform application and cached across frames
  - collision hitbox is intentionally still set manually for gameplay tuning/stability
  - callback contract/events to tiny-clj scheduler remain part of this milestone TODO

Done when:

- Collision pairs are specified declaratively via stable ids in scene records.
- C collision checks run automatically from snapshot input using Clojure-declared pair specs and dispatch deterministic callbacks into the existing Clojure scheduler.

## Optional Extension A: Render-Thread Interpolation Animations (Off-Main-Thread)

Status: OPTIONAL-LATER (generic SPSC queue + fixed-point animator math prerequisites implemented; animation command/event wiring still TODO)

Relationship to Milestone 9: This extension builds on the three-layer target architecture
(M9). The interpolation layer (Layer 2) is the foundation; this extension adds declarative
animation descriptors that drive the interpolation automatically. Key open question from M9d:
animations must run in C (not interpreter) – this extension defines how.

Scope:

- Additive extension after stable snapshot/slot pipeline; does not replace scene snapshot model.
- Goal: keep continuous motion smooth even when cooperative Clojure scheduler is temporarily blocked.

Tasks:

- Define animation record contract (declarative timeline):
  - `:target-id` (stable object id)
  - `:property` (initially transform-like only: `:tx`, `:ty`, `:rot`, `:sx`, `:sy`)
  - `:from`, `:to`
  - `:start-ts-ms`, `:duration-ms`, `:easing`
  - optional `:on-finish` event/callback token
- Implement C-side interpolation on render thread:
  - evaluate active animation value by render-time clock
  - apply interpolated value transiently at draw time (no scene topology mutation in render thread)
  - prefer fixed-point interpolation/easing in the animator path (`CLJ_FIXED_FRAC_BITS`) and quantize only at raster/application boundaries
  - rationale: reduce float conversion overhead and keep animation playback deterministic across host/device
- Keep strict thread ownership:
  - producer thread publishes/updates animation descriptors
  - render thread reads descriptors and computes interpolation only
  - no MEMORY macros/autorelease pool usage in render hot path
- Add bounded lock-free queue handoff (SPSC preferred):
  - producer/update thread -> render thread: animation control commands (`start`, `cancel`, `replace-track`)
  - render thread -> producer/event-loop: completion events (`:anim-finished`, optional `:anim-cancelled`)
  - no per-frame progress messages; only coarse control + one-shot completion notifications
  - define overflow policy (drop oldest/newest vs assert in debug) and counters for observability
  - current implementation note (prerequisite done):
    - generic bounded SPSC queue module exists in C (`lockfree_spsc_queue`)
    - audio engine command + finished queues already use the generic queue as a production-tested reference path
    - audio hot path uses direct generic queue calls (no forwarding wrappers)
    - fixed-point animator math helpers (`progress`, `easing`, `lerp`, Q19.13 / `CLJ_FIXED_FRAC_BITS`) exist in `vector_scene_graph` and are covered by host unit tests
    - host viewer demo already uses the fixed-point animator math helpers for existing movements (`player_bob`, `obstacle_x`) as an integration proof
- Add scheduler callback integration:
  - optional deterministic event when animation reaches end (`:anim-finished`)
  - callback payload includes at least `:target-id`, `:property`, end timestamp
- Add regression/perf tests:
  - smoothness under producer-thread stalls
  - deterministic interpolation for fixed timestamps
  - no duplicate finish events

Done when:

- Continuous transform animations remain smooth under temporary scheduler stalls.
- Extension remains additive and compatible with snapshot-based scene publication.

## Non-Goals (explicitly excluded)

- SVG XML parsing
- CSS or style inheritance system
- Filters, gradients, masks
- Bézier curves in initial implementation
- Alpha blending and full anti-aliasing pipeline
- Complex text shaping/layout engine

## Recommended Implementation Order (first 2 weeks)

1. M0 scene contract + record schema
2. M1 macOS simulator skeleton + deterministic frame dumps
3. M2 transform stack + composition
4. M8 fixed-first decode + transform policy (determinism + ESP32 CPU baseline)
5. M3 baseline primitives (`width=1`)
6. M4 thick line support
7. M4b solid fill-color MVP (`has_fill` + `fill_rgb565`, no fill-rule yet)
8. M8b batched scene update API (`update-nodes` for efficient per-frame state changes)
9. M5 snapshot slot update path (`FrameScene` + `clip-rect` + changed-slot-only render)
10. M6 VText integration
11. M7 SPI backend integration (slot windows on SPI)
12. M9 game/menu/title integration
13. M10 collision contract + C-driven callback
14. Optional later: explicit patch path + pointer-identity subtree reuse

Rule:

- Do not start M7 (ESP32 SPI integration) before M5 snapshot-slot rendering passes in host simulator.

## Risk Register

- Risk: Thick stroke rasterization consumes too much frame budget.
  - Mitigation: axis-aligned fast paths, simple default caps/joins, command caps.
- Risk: Simulator behavior diverges from ESP32 backend behavior.
  - Mitigation: shared render core, backend conformance tests, deterministic frame traces.
- Risk: Transform stack overhead in deep group trees.
  - Mitigation: shallow graph conventions, iterative traversal, compact matrix representation.
- Risk: SPI bandwidth limits visible complexity.
  - Mitigation: clipping/culling, dirty-region mode, avoid pathological tiny draw commands.
- Risk: tiny-clj allocation spikes during snapshot rebuilds.
  - Mitigation: persistent data sharing, coarse scene slots, changed-slot-only rendering, optional compiled caches later.
- Risk: Slot rectangles appear non-overlapping logically but overlap in raster output due to stroke/text fringes.
  - Mitigation: conservative `clip-rect` guard bands, explicit slot background policy, test coverage for edge clipping.
- Risk: Float drift/non-determinism in long animations.
  - Mitigation: fixed-first decode/transform path aligned with `subjective-c` fractional bits (`CLJ_FIXED_FRAC_BITS = 13`), integer quantization only at raster boundaries.

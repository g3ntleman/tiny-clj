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
- Deterministic immediate rendering on SPI displays (ST7789/ILI9341 class) or 8-bit parallel I80 displays after host PoC is stable
- Thick-stroke primitives suitable for game, menu, and animated vector title screens
- Solid fill color for area primitives (SVG-like paint model MVP)
- Generalized Clojure collision API: rules and callback routing are declared/configured in Clojure, while per-frame collision detection remains in C
- Collision callbacks are Closure-based and dispatched from C via scheduler/runloop ingress; callback return values are ignored

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
- `**update-nodes` (M8b) no longer needed** for typical updates; direct `assoc-in` replaces tree walking.

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

### Threading Model

The architecture requires exactly **two application threads**. A third thread exists
only on macOS as a platform quirk (Cocoa requires the OS main thread for UI).

```
┌─────────────────────────────────────────────────────────────────────────┐
│  Thread 1: Tiny-RTOS / Clojure  (= application main thread)            │
│                                                                         │
│  • Runs tiny-clj scheduler, game logic, event handling                  │
│  • Owns all scene state as flat entity maps in Atoms (one per slot)     │
│  • Updates via swap!/reset! – never blocks                              │
│  • Starts/stops the render thread via Clojure functions:                │
│      (start-renderer! [game-slot score-slot deco-slot])                 │
│      (stop-renderer!)                                                   │
│                                                                         │
│  On ESP32: RTOS task (the one running tiny-clj)                         │
│  On macOS: any thread (not necessarily the OS main thread)              │
└────────────────────────┬────────────────────────────────────────────────┘
                         │ atom_deref per slot (lock-free pointer read)
                         ▼
┌─────────────────────────────────────────────────────────────────────────┐
│  Thread 2: Render Thread  (spawned/stopped by Clojure)                  │
│                                                                         │
│  • At frame start: atom_deref per slot → immutable snapshot             │
│  • Renders entire frame from consistent snapshot:                       │
│    resolve Timelines, interpolate AnimState, rasterize                  │
│  • Frame done → loop back, pick up latest snapshot for next frame       │
│  • Read-only on Clojure data – no ASSIGN, no RETAIN/RELEASE            │
│                                                                         │
│  On ESP32: writes framebuffer directly to SPI/DMA → display             │
│  On macOS: writes into RGB565 framebuffer (same as ESP32 GRAM)          │
└────────────────────────┬────────────────────────────────────────────────┘
                         │ (macOS only: live framebuffer read — no copy)
                         ▼
┌─────────────────────────────────────────────────────────────────────────┐
│  macOS only – Cocoa UI Thread  (platform requirement, not architectural)│
│                                                                         │
│  • Reads live fb_pixels directly (lock-free, no copy buffer)            │
│  • Expands RGB565 → XRGB8888 for window presentation (mfb_update_ex)   │
│  • Simulates ESP32 SPI/I80 display: bus reads live GRAM, no double-buf  │
│  • Tearing is possible and accepted — same as on real hardware          │
│  • MiniFB event loop, input forwarding, frame pacing, metrics           │
│  • No scene logic, no Record mutation                                   │
│                                                                         │
│  Does not exist on ESP32 – render thread drives display directly.       │
└─────────────────────────────────────────────────────────────────────────┘
```

**No additional RTOS threads needed** for the scene-graph engine. Other subsystems
(audio, networking, sensor polling) may have their own threads but are orthogonal to this plan.

**Key property:** The two threads are decoupled via immutable snapshots in Atoms.
No mutex is needed for scene data exchange – `atom_deref` is a lock-free pointer read.
On macOS, the Cocoa UI thread reads the live framebuffer directly (no mutex, no copy buffer).
This faithfully simulates how ESP32 SPI/I80 displays read the live GRAM without double-buffering.

### Layer Separation

```
┌─────────────────────────────────────────────────────────────┐
│  Thread 1: Tiny-RTOS / Clojure (= main thread)             │
│                                                             │
│  (def game-slot (atom {root (group ...) 3001 (tri ...) ..}))│
│  (start-renderer! [game-slot score-slot deco-slot])         │
│                                                             │
│  • Flat map of {id → Record} per slot, in an Atom           │
│  • State changes via swap!/assoc-in (event-driven)          │
│  • Timeline Records on fields for periodic animations       │
│  • No Clojure eval in the render loop                       │
└────────────────────────┬────────────────────────────────────┘
                         │ atom_deref (once per frame, lock-free)
                         ▼
┌─────────────────────────────────────────────────────────────┐
│  Thread 2: Render Thread (spawned by Clojure)               │
│                                                             │
│  • Snapshots flat entity maps from Atoms at frame start     │
│  • Resolves Timeline fields by wall clock (modulo arith.)   │
│  • Interpolates transform targets (lerp/easing for smooth   │
│    motion) using mutable C-owned AnimState structs          │
│  • Owns animation timing (dt, framerate-independent)        │
│  • Traverses logical tree (root → children IDs → lookup)    │
│  • Composes inherited transforms                            │
│  • Rasterizes primitives with resolved field values         │
│  • Read-only on the Clojure snapshot                        │
│  • ESP32: writes directly to SPI/DMA → display GRAM         │
│  • macOS: writes into shared fb_pixels (live GRAM simulation)│
└─────────────────────────────────────────────────────────────┘
```

### Design Principles

- **Clojure never mutates Records.** State changes produce new immutable Records via `swap!`.
- **C never mutates the Clojure snapshot.** C reads via `atom_deref` (lock-free pointer read).
- **Records over Maps** for entities: C knows Record layout (`DEFRECORD`), field access is O(1).
- **No Clojure eval per frame.** Clojure sets state event-driven (collision, timer, input).
C resolves Timelines and interpolates every frame with fixed time budget.
- **Snapshot-per-frame.** Render thread snapshots all slot Atoms once at frame start.
The entire frame renders from this consistent state. Intermediate Clojure updates
are picked up at the next frame start – scene data is always consistent per frame.
Note: the display itself has no double-buffer (SPI/I80 GRAM is single-buffered),
so visual tearing at the display level is possible and accepted — same as real hardware.
- **Static slots skip rendering.** If a slot has no Timelines and no active AnimState,
its output is frame-identical. Render thread skips erase + re-render until
Clojure publishes a new snapshot. If all slots are static, render thread sleeps.
- **Interruption-safe.** New target mid-animation → C interpolates from current visual
position toward new target (no jump, no restart).
- **Timelines are data, not code.** Periodic animations (form cycling, blinking, color pulsing)
are Timeline Records on fields – C evaluates them with pure arithmetic, no timers needed.
- **Two threads, Clojure-controlled.** Tiny-RTOS/Clojure is the main thread. Render thread
is started/stopped from Clojure via `(start-renderer!)` / `(stop-renderer!)`.
macOS Cocoa UI thread is a platform detail, not an architectural thread.
No additional RTOS threads needed for the scene-graph engine.
- **No game logic in C.** All game/app logic lives in Clojure (target states via `swap!`).
C render thread only interpolates and rasterizes. No demo-specific gameplay functions in C.
- **Animation descriptors (TODO).** Declarative transition animations (easing, duration, from/to
for smooth motion) should also execute in C. Exact contract to be designed after flat entity
map and Timeline are stable (see Optional Extension A for initial sketch).

## Constraints

- MCU class: ESP32 (resource-constrained embedded target)
- Display class: SPI (ST7789/ILI9341, 240×240 or 320×240) or 8-bit parallel I80 (faster, rarer)
- Display has a single internal GRAM, no double-buffer — tearing is possible and accepted
- macOS host simulator is mandatory before device bring-up
- Shared render core between simulator and ESP32 (avoid logic forks)
- Host emulation must happen at driver/backend level (not above primitive rasterization),
so primitive generation/rasterization bugs are debuggable on host.
- Host display simulation must faithfully match ESP32 display behavior:
  - no double-buffer on the display side
  - render thread writes directly into a single framebuffer (GRAM equivalent)
  - main/UI thread reads the live framebuffer without mutex or copy
  - tearing is possible and accepted — same as on real hardware
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
  - render thread acquires scene snapshots via lock-free `atom_deref` at frame start;
  renders the full frame from the consistent snapshot; picks up new state at next frame start
- Thread ownership rule (target architecture – two threads):
  - Thread 1 – Tiny-RTOS / Clojure (= main thread): owns scene state (Atoms with flat entity
  maps), all game/app logic, starts/stops render thread via `(start-renderer!)` / `(stop-renderer!)`
  - Thread 2 – Render thread (spawned by Clojure): owns interpolation (AnimState), Timeline
  resolution, rasterization; read-only on Clojure snapshots; on ESP32 drives SPI directly
  - macOS Cocoa UI thread: platform quirk, presentation only – not an architectural thread
  - No game logic in C; no direct ASSIGN into scene Records from any C thread
- Backend-parity rule (host <-> ESP32):
  - define one shared backend submission interface (e.g. `begin_frame`, `submit_rect`, `end_frame`)
  - render thread owns backend submission on both targets
  - macOS main/UI thread is presentation-only (`mfb_update_ex`), matching ESP32 where render thread drives SPI/I80 writes
- Display simulation rule (host):
  - host viewer uses a single RGB565 framebuffer (no copy buffer, no double-buffer)
  - render thread writes into the framebuffer; UI thread reads it live without synchronization
  - this mirrors real SPI/I80 display behavior: the display bus reads from the live GRAM while the MCU writes into it
  - tearing artifacts on the host are intentional and match what users see on real hardware
  - dirty-rectangle tracking and static-slot skip remain the primary bandwidth optimization — same as on ESP32 SPI

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

## Milestone 7: ESP32 Display Backend Integration (Post-PoC)

Status: TODO

Supported display interfaces:

- **SPI** (primary): ST7789, ILI9341 — serial bus, 40–80 MHz clock, bandwidth-limited
- **8-bit parallel I80** (optional): faster bus, more pins, less common/more expensive
- Both interfaces drive displays with a single internal GRAM and no double-buffer
- Host viewer faithfully simulates this: single RGB565 framebuffer, no copy, no mutex in present path

Tasks:

- Integrate validated raster output with SPI/I80 transport model:
  - set window + burst writes (CASET/RASET/RAMWR for SPI; 8080 command sequence for I80)
  - DMA transfer for bulk pixel data (SPI DMA or I80 DMA where available)
- Introduce/consume shared backend submission interface used by both host and ESP32:
  - backend command surface (frame begin/end + dirty-rect submission)
  - keep renderer/backend boundary identical across targets
  - no host-only rendering branch above this interface
- Map each render slot `clip-rect` to display address windows (one window per dirty slot in the simple path).
- Keep backend boundary identical between host and ESP32 so host can emulate the device driver layer
while still exercising the same primitive rasterization pipeline.
- Keep full framebuffer optional (not mandatory).
- Implement minimal clipping/culling before draw submission.
- Add slot-dirty update mode:
  - only clear/render/send changed slots
  - prefer a small number of larger slot windows over many tiny widget windows
- Leverage static-slot optimization from M9 for SPI throughput:
  - static slots (no Timelines, no active AnimState) produce frame-identical output
  - render thread skips erase + rasterize + SPI transfer for static slots entirely
  - SPI bandwidth is reserved for animated slots only
  - typical layout: 2 of 3 slots static (score HUD, deco landscape) → ~60% fewer SPI transfers per frame
  - when all slots are static, no SPI writes at all (render thread sleeps until Atom change)
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
  - UI thread reads live framebuffer directly (no copy buffer, no mutex) — simulates SPI/I80 bus reading GRAM
  - host backend path follows the same dirty-rect submission contract as ESP32 SPI/I80 path
- SPI/I80 bandwidth optimization strategies (research-backed):
  - **Dirty-rectangle tracking**: only transmit changed pixels — typical action games change 40–60% of pixels per frame (fbcp-ili9341: Quake averages 46%)
  - **Static-slot skip**: HUD/score slots rarely change → 0 bytes transferred for static frames
  - **Span merging**: merge adjacent dirty regions on same scanline to reduce CASET/RASET command overhead
  - **DMA pipeline**: render thread prepares next frame while DMA transfers current dirty regions
  - **Adaptive interlacing** (optional): if dirty pixel count exceeds bus capacity, alternate even/odd scanlines across frames
  - **TE-pin synchronization** (optional): if display exposes TE signal, synchronize write start with display scan to reduce visible tearing
  - **Write speed rule**: tearing-free operation requires write speed ≥ 50% of display read speed (Espressif docs)

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
  - writes submitted RGB565 dirty regions directly into the live framebuffer (GRAM simulation)
  - UI/main thread reads the live framebuffer for presentation (`mfb_update_ex`) — no copy, no lock
  - faithfully simulates ESP32 single-GRAM display without double-buffering
- ESP32 backend implementation:
  - maps each submitted rect to SPI window + burst write (CASET/RASET/RAMWR) or I80 command sequence
  - uses DMA for bulk pixel transfer to keep CPU free for render-thread work
  - no full-frame requirement in the default path
- Determinism/conformance:
  - for identical scene snapshots, host and ESP32 must observe equivalent rect submission order/data contract
  - backend conformance tests compare emitted rect sequences for representative scenes

Done when:

- Scene renders on device without full-frame requirement and with pacing consistent with simulator logic.
- Shared backend submission interface is used on both targets, and conformance checks pass for representative scenes.
- Host viewer faithfully simulates target display behavior (single GRAM, no double-buffer, live framebuffer reads).
- Dirty-rectangle tracking reduces SPI/I80 transfer volume for typical game scenes to <60% of full-frame.

## Milestone 7b: Bandwidth Optimization (Entity-Level Dirty Tracking)

Status: TODO

Motivation:

- SPI displays are bandwidth-limited (240×240@16bit@40MHz ≈ 43fps full-frame).
Every saved pixel directly increases achievable frame rate or frees bus time for other SPI devices.
- The current renderer erases the **entire slot clip-rect** to background color before redrawing
all entities — even entities that haven't changed (e.g. static text "Game Scene").
- On ESP32, this wastes both CPU cycles (rasterizing unchanged geometry) and SPI bandwidth
(transferring pixels that are identical to the previous frame).
- Immutable persistent data structures provide **free entity-level change detection** via
pointer identity: `old_entity == new_entity` means the entity is visually identical.

### Core Mechanism: Pointer-Identity Change Detection

```clojure
;; Clojure: (swap! game-slot assoc-in [3002 :t] new-transform)
;; → Entity 3002: NEW Record (different pointer)
;; → All other entities: SAME Record (structural sharing)
```

```c
// C: O(1) per entity — pointer comparison
ID old_entity = map_get(prev_entity_map, entity_id);
ID new_entity = map_get(curr_entity_map, entity_id);
bool changed = (old_entity != new_entity);  // structural sharing → free diff
```

Two kinds of dirty detection:

1. **Snapshot dirty**: Entity pointer changed → Clojure updated it via `swap!`/`assoc-in`.
2. **Animation dirty**: Entity has Timeline fields → visual output changes every frame
  even though the entity pointer stays the same. Detected via per-entity `has_animation` flag.

### Entity Bounding Box Cache

During `render_record_node`, compute and cache the axis-aligned bounding box (AABB)
of each rendered entity in framebuffer coordinates:

```c
#define VG_MAX_CACHED_ENTITY_BBOXES 64

typedef struct {
    uintptr_t entity_id;
    VgClipRect bbox;
    bool has_animation;
} VgEntityBBoxEntry;

typedef struct {
    VgEntityBBoxEntry entries[VG_MAX_CACHED_ENTITY_BBOXES];
    uint32_t count;
} VgEntityBBoxCache;
```

The bbox includes the full rendered extent (stroke width, text fringe, transform).

### Render-Thread Frame Flow (with dirty tracking)

All dirty-tracking logic runs **entirely in the render thread**. The main thread
only reads `g_gram_pixels` for display — it never sees or needs dirty rect data.

```
viewer_render_thread_main (per frame, per slot):
│
├─ 1. SNAPSHOT: atom_deref(slot_atom) → curr_entity_map
│
├─ 2. DIFF (pointer identity):
│     for each (id, entity) in curr_entity_map:
│       old = lookup(prev_entity_map, id)
│       if entity != old → mark dirty        // snapshot dirty
│       else if bbox_cache[id].has_animation → mark dirty  // animation dirty
│     for each id in prev but not in curr → mark dirty     // removed
│
├─ 3. COLLECT dirty rects:
│     for each dirty entity:
│       dirty_rect[i] = union(old_bbox[i], new_bbox[i])
│
├─ 4. MERGE overlapping rects → merged_rects[] (O(n²), n < 10)
│
├─ 5. ERASE + REDRAW in g_render_buffer:
│     for each rect in merged_rects:
│       clear(g_render_buffer, rect, clear_color)
│       for each entity where bbox ∩ rect ≠ ∅:
│         render(entity, clip=rect)
│
├─ 6. TRANSFER to GRAM (simulate SPI):
│     for each rect in merged_rects:
│       copy_rect(g_render_buffer → g_gram_pixels, rect)
│       transferred_pixels += rect.area
│
├─ 7. DEBUG OVERLAY (if show_dirty_rects flag set):
│     for each rect in merged_rects:
│       draw_rect_outline(g_gram_pixels, rect, 0x07E0)   // green, GRAM only
│
├─ 8. UPDATE state:
│     prev_entity_map = curr_entity_map
│     bbox_cache = new bboxes from step 5
│     atomic_store(last_dirty_pixels, transferred_pixels)
│
└─ 9. atomic_store(frame_serial++)  // main thread sees new frame
```

The `show_dirty_rects` flag is an `atomic_bool` set by the main thread on D-key press,
read by the render thread — no synchronization beyond the atomic.

### SPI Transfer Model

SPI display controllers (ILI9341, ST7789, etc.) fill address windows sequentially —
there is no skip or seek within a window. Every pixel position within
`CASET/RASET` must be sent. Consequence:

- A single union rect of all dirty areas wastes bandwidth on unchanged pixels
between spatially separated dirty entities (worst case = full slot = no optimization).
- **Separate address windows per merged dirty rect** minimize transfer volume.
Command overhead per window is ~10 bytes (`CASET` + `RASET` + `RAMWR`),
negligible vs. saved pixel data at 40 MHz SPI.

```
Example: player (40×30 @ top-left) + obstacle (20×60 @ bottom-right)

Union rect:  280×200 = 56.000 px = 109 KB   ← almost full frame, no savings
Separate:    40×30 + 20×60 = 2.400 px = 4,7 KB  ← 96% saved
```

### Expected Impact (Game Slot Example)


| Entity       | Type     | Dirty?         | Action                   |
| ------------ | -------- | -------------- | ------------------------ |
| root         | Group    | —              | Traversal container      |
| terrain      | Polyline | Yes (Timeline) | Erase old bbox → Redraw  |
| player       | Tri      | Yes (Timeline) | Erase old bbox → Redraw  |
| obstacle     | Polyline | Yes (Timeline) | Erase old bbox → Redraw  |
| "Game Scene" | VText    | **No**         | **Skip** — pixels remain |


Dirty pixels reduction: ~30–50% fewer pixels per frame for typical game scenes.
CPU savings: static entities are neither erased nor rasterized.

### Tasks

1. **Entity BBox computation + cache** (`scene.c` / `vector_scene_graph.h`):
  - Compute AABB during `render_record_node` for each primitive type.
  - Store per-entity bbox in `VgEntityBBoxCache` (indexed by entity ID).
  - Include stroke width and transform in bbox computation.
  - Cache lives in `VgRenderSlotState` (per slot, persistent across frames).
2. **Previous entity map retention** (`VgRenderSlotState`):
  - Store pointer to previous flat entity map in slot state.
  - On new frame: compare old vs new map entries by pointer identity.
  - After rendering: update stored map pointer to current snapshot.
3. **Dirty-rect collection + merge** (`scene.c` or utility):
  - Per dirty entity: `dirty_rect = union(old_bbox, new_bbox)`.
  - Merge overlapping rects into minimal disjoint set (O(n²), n < 10).
  - Output: `VgClipRect merged_rects[]` + `uint32_t count`.
4. **Per-rect erase + overlap-aware redraw** (`scene.c`):
  - Replace full-slot `vg_framebuffer_clear_rect(dirty, clear_color)` with
   per-merged-rect erase + redraw loop.
  - For each merged rect: erase to clear color, then render all entities
  whose bbox intersects the rect (dirty + clean overlapping entities),
  clipped to the rect boundary.
  - For `force_render` (animation tick, same snapshot): only process entities
  with `has_animation=true`.
  - For snapshot change: process entities whose pointer changed + removed entities.
5. **Per-rect GRAM transfer** (`host_viewer_minifb.c` / ESP32 backend):
  - Replace full-frame `memcpy` to GRAM with per-rect row-copy
   from `g_render_buffer` to `g_gram_pixels` (render thread).
  - On ESP32: each rect → `CASET` + `RASET` + `RAMWR` + DMA pixel stream.
  - Accumulate `transferred_pixels` (sum of merged rect areas) for metrics.
6. **Debug overlay** (`host_viewer_minifb.c`, render thread):
  - `atomic_bool show_dirty_rects` — set by main thread on D-key, read by render thread.
  - After per-rect GRAM copy, draw 1px green (0x07E0) outlines around each
  merged dirty rect directly into `g_gram_pixels` (GRAM only — does not
  persist into render buffer, disappears when overlay is toggled off).
  - Title shows `[D]` indicator when active.
7. **Tests + metrics**:
  - Unit tests: entity-level dirty detection with synthetic snapshots.
  - Unit tests: rect merge algorithm (overlapping, disjoint, nested cases).
  - Regression tests: visual output unchanged for representative scenes.
  - Metrics: `transferred_pixels` per frame (sum of merged rect areas).
  - Host-viewer title: `bw` shows actual transfer volume, not full-frame size.

### Complications

- **Overlapping entities**: If entity A moves and its old bbox overlaps entity B,
B must be redrawn even though B didn't change. The per-rect redraw loop
includes all entities whose bbox intersects the merged dirty rect.
- **Timeline entities**: Pointer is the same but visual output changes every frame.
Per-entity `has_animation` flag (already tracked during rendering) identifies these.
- **Entity addition/removal**: New entities have no old bbox (just draw).
Removed entities have no new bbox (just erase old bbox).
- **Slot property changes** (clip-rect, clear-color, visibility): Fall back to full-slot erase.

Done when:

- Static entities within animated slots are neither erased nor redrawn.
- Each merged dirty rect is transferred as a separate SPI address window
(simulated on host via per-rect memcpy in the render thread).
- Transferred pixels per frame are measurably reduced for scenes with mixed static/animated content.
- Visual output is identical to full-erase rendering (no ghost pixels, no missing redraws).
- Debug overlay (D key) shows green outlines around dirty rects without affecting render state.
- Host-viewer `bw` metric reflects actual per-rect transfer volume.

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
  - Convert to integer only at explicit raster boundaries (`gfx_draw`_*, clip/window setup, framebuffer index math).
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

Status: DONE (`9a` + `9b` + `9c` + `9d` baseline DONE on host/ESP32 build path; renderer lifecycle + rendered-state query baseline DONE; host loop closeout + cleanup complete; collision callback API finalized in `tiny-gfx.collision`)
Follow-up (2026-03-05): API parity + Clojure docs hardening in `tiny-gfx.`* is now tracked under `9n`.

Target: Migrate host-viewer to the flat-entity-map architecture (see "Target Architecture"):

- Each slot = one Atom holding `{id → Record}` (flat, not nested)
- Root entity has `:id root` (symbol)
- Groups reference children by ID
- Timeline Records on fields for periodic animations (C-evaluated, no timers)
- C reads snapshot via `atom_deref`, resolves Timelines, interpolates, renders

Final state:

- Flat entity maps and Timeline decode are now in place, and collision response (triangle toggle) is already executed in Clojure callback code.
- Collision callback dispatch now routes through `tiny-gfx.collision/invoke-collision-callback!` without string-eval hardcoding in the host-viewer C loop; callback target selection stays Clojure-configurable.
- Callback execution model is one-way dispatch: C enqueues/invokes configured Clojure closure through runloop ingress and ignores return values.
- Scene updates are explicit on the Clojure side (`swap!`/`reset!`); C never expects a returned `FrameScene` from callback execution.
- Per-frame collision detection remains in C (required); demo-specific mutation coupling has been removed from C.

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
- Implementation notes (2026-03-05):
  - Added fixed-point `VgAnimTransformState` API in `vector_scene_graph`:
  `vg_anim_transform_state_reset`, `vg_anim_transform_state_set_target`,
  `vg_anim_transform_state_step`, `vg_anim_transform_state_current`.
  - Added deterministic unit tests for convergence, interruption-safe target switching,
  and zero-duration snap behavior.
  - Host viewer gameplay step now uses `AnimState` for terrain/player/obstacle transforms.
  Interpolation step is integer/fixed-point only (`dt_ms`/`now_ms`), no float math in the interpolation path.

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

### 9f: Generalized main loop + host-viewer migration

**Generalized C main loop (target):**

- The main loop becomes app-agnostic – no demo-specific gameplay logic in C.
- Main loop responsibilities: frame pacing, input forwarding, framebuffer presentation, metrics.
- All game/app logic moves to Clojure (scene state updates via `swap!` on slot Atoms).
- Current demo-specific code to remove from C main loop:
  - `viewer_apply_gameplay_step()` (terrain scroll, player jump, obstacle position, collision)
  - `set_transform_fields()`, `set_player_geometry()`, `set_obstacle_transforms()`
  - Direct `ASSIGN(demo_bundle.score_text->text, ...)` calls
  - `ViewerDemoBundle` struct and all per-entity handle tracking

**Render thread (target):**

- At frame start: `atom_deref` per slot Atom → immutable snapshot (lock-free).
- Render entire frame from snapshot: resolve Timelines, interpolate AnimState, rasterize.
- Frame done: signal main thread, loop back to frame start with fresh snapshot.

**Static vs. animated slot optimization:**

- During rendering, the render thread determines per slot whether the scene contains
any active Timelines or AnimState interpolations (= **animated**) or not (= **static**).
- **Static slot:** No Timelines, no AnimState in progress. The rendered output is
identical across frames. The render thread skips erase + re-render for this slot
in subsequent frames until:
  - Clojure publishes a new snapshot (`swap!`/`reset!` changes the Atom value), or
  - an AnimState target changes (which also implies a new snapshot).
- **Animated slot:** Contains at least one Timeline or active AnimState interpolation.
Must be re-rendered every frame (Timelines depend on wall clock, AnimState converges
over time).
- Detection is cheap: during traversal, set a `slot_has_animation` flag when any
Timeline field or non-converged AnimState is encountered. Cache the flag per slot.
- This is the generalized version of the existing slot-change-tracker concept:
static slots behave like unchanged slots (skip rendering), animated slots always
re-render regardless of whether the Clojure snapshot changed.
- Power savings: if **all** slots are static, the render thread can block/sleep until
any Atom changes (event-driven wakeup, no busy loop).

**Clojure side (target):**

- `tiny-gfx.host-viewer-demo` defines all scene entities as flat maps per slot.
- Game logic (terrain scroll, player jump, obstacle, collision, score) runs as
Clojure functions triggered by `schedule-periodic` timers or input events.
- Each game tick: `(swap! game-slot assoc-in [entity-id :t] new-transform)`.
- Implementation notes (2026-03-04):
  - Demo includes first periodic Timeline slice: `hbar` uses looping transform Timeline.
  - Host viewer no longer updates `hbar` via C-side `ASSIGN`.
  - 2026-03-05 follow-up: direct score-text mutation (`ASSIGN(demo_bundle.score_text->text, ...)`) removed from host-viewer loop.
  - 2026-03-05 follow-up: host-viewer demo switched terrain/player/rocket motion to Clojure-authored Timeline transforms; C-side `viewer_apply_gameplay_step()` removed from main loop.
  - 2026-03-05 follow-up: score text switched to Timeline-driven updates in Clojure demo data (`score-text-timeline`), no C-side score writes.
  - 2026-03-05 follow-up: removed score-slot periodic republish from host-viewer main loop; Timeline-driven score animation now advances without demo-specific publish logic.

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

### 9i: Rendered-State Query API (Clojure → Render Thread)

The Clojure Atom holds the **target** state. The render thread holds the **current visual**
state (after interpolation + Timeline resolution). Game logic often needs the current visual
state – e.g. "has the player reached position X?", "is the animation done?".

**Rendered-State Snapshot:**

- After each frame, the render thread writes resolved per-entity values into a shared
snapshot structure (one per slot, indexed by entity ID).
- Clojure reads this snapshot via native functions (lock-free read of last-completed frame).
- Snapshot is updated atomically per frame – Clojure always sees a consistent frame state.

**Clojure API:**

```clojure
;; Current visual transform of an entity (after interpolation):
(renderer-state slot entity-id)
;; => {:tx 45 :ty -12 :rot 0 :sx 1 :sy 1}

;; Current Timeline keyframe index for a specific field:
(renderer-timeline-step slot entity-id :pts)
;; => 1  (second keyframe currently active)

;; Timeline progress through full period [0.0 .. 1.0]:
(renderer-timeline-progress slot entity-id :pts)
;; => 0.7  (70% through the cycle)
```

**Use cases:**

- Game logic: "has entity reached target?" → compare `renderer-state` with target atom
- Collision: based on actual visual position, not target
- Animation control: `(renderer-timeline-progress ... :pts)` = 1.0 means done (for `loop=false`)
- REPL debugging: inspect live visual state

**Implementation sketch (C side):**

- Render thread maintains per-slot `RenderedEntityState` array/map.
- After resolving each entity during traversal, writes:
  - composed world-space transform (`tx, ty, rot, sx, sy`)
  - active Timeline keyframe index + phase offset per animated field
- Published atomically (pointer swap or generation counter) after frame completes.
- Native functions `renderer_state`, `renderer_timeline_step`, `renderer_timeline_progress`
read from the last-published snapshot.

**Threading safety:**

- Render thread writes → atomic publish (pointer swap).
- Clojure reads → lock-free deref of last-published snapshot.
- No mutex needed. Clojure may see a 1-frame-old snapshot (acceptable).

Implementation notes (2026-03-05):

- Added runtime query natives:
  - `tiny-clj.runtime/renderer-state`
  - `tiny-clj.runtime/renderer-timeline-step`
  - `tiny-clj.runtime/renderer-timeline-progress`
- Added lock-free double-buffered rendered-state snapshots (`rendered_state_snapshot`) published by render thread per slot/frame.
- Scene traversal now records per-entity world transform matrices and active Timeline metadata (field + step/phase/period) into the rendered snapshot when a slot is rendered.
- Host-viewer render loop now starts/commits/discards rendered-state capture alongside per-slot render calls.

### 9j: Remaining task list (execution order)

1. **Main-loop entkoppeln (host-viewer):**
  - Remove demo-specific gameplay writes from C main loop:
    - `viewer_apply_gameplay_step`
    - `set_transform_fields` / `set_player_geometry` / `set_obstacle_transforms`
    - direct score-text `ASSIGN`
  - Keep C main loop app-agnostic (pacing, input forwarding, presentation, metrics only).
2. **Clojure-driven scene updates finalisieren:**
  - Move remaining gameplay target updates (`terrain`, `player`, `obstacle`, score text) into Clojure slot updates.
  - Keep update contract: per-slot atomic snapshot via `swap!`/`reset!` on flat maps.
  - Status (2026-03-05): DONE for host-viewer demo vertical slice (terrain/player/rocket/score moved to Timeline-driven Clojure scene data; regression tests added).
3. **Renderer lifecycle API aus Clojure:**
  - Add native API and docs:
    - `(start-renderer! [game-slot score-slot deco-slot])`
    - `(stop-renderer!)`
  - Ensure deterministic start/stop semantics and clean shutdown ordering.
  - Status (2026-03-05): baseline runtime API added (`tiny-clj.runtime/start-renderer!`, `tiny-clj.runtime/stop-renderer!`)
  with deterministic idempotent bool semantics; default runtimes return `false` when no backend is registered.
  Host-viewer now installs lifecycle callbacks through a shared renderer-lifecycle bridge and uses the same start/stop path.
4. **Rendered-state query API implementieren (9i):**
  - C-side snapshot structs + atomic publish per frame.
  - Native Clojure functions:
    - `renderer-state`
    - `renderer-timeline-step`
    - `renderer-timeline-progress`
  - Verify lock-free read contract and 1-frame staleness behavior.
  - Status (2026-03-05): baseline DONE on host path (double-buffer snapshot publish + runtime query natives + unit tests).
5. **Tests + acceptance gates for M9:**
  - Unit tests: AnimState + Timeline interaction, rendered-state query correctness.
  - Integration tests: app-agnostic loop path, renderer start/stop from Clojure, no C gameplay writes.
  - Host run: verify continuous rendering with Clojure-driven updates and stable performance counters.
6. **Clojure-konfigurierbarer Collision-Toggle-Callback (neu):**
  - Add a Clojure function to configure which callback is invoked on collision toggle.
  - Remove hardcoded callback expression from C (`VIEWER_COLLISION_CALLBACK_EXPR`).
  - Keep C side generic: invoke configured callback closure via scheduler/runloop ingress (no `FrameScene` return coupling).
7. **Demo-Logik vollständig in Clojure verlagern (neu):**
  - Keep collision detection + cooldown/latch evaluation per frame in C.
  - Move collision **response** (scene mutation, gameplay state updates) to Clojure callbacks.
  - C host loop keeps only renderer/pacing/presentation/input plus generic collision callback dispatch.

### 9k: End-of-M9 code cleanup (required)

After all M9 features are implemented, do a cleanup pass before declaring M9 done:

- Remove dead code and compatibility shims from host-viewer:
  - obsolete demo-specific helpers and structs
  - stale ASSIGN-based mutation paths
  - temporary migration flags no longer needed
- Consolidate naming/docs around the final architecture:
  - Tiny-RTOS/Clojure main thread vs render thread responsibilities
  - rendered-state query API docs + examples
- Keep only one canonical path in runtime code for:
  - slot snapshot publication
  - render-thread snapshot consumption
  - Clojure API for renderer lifecycle
- Run formatting/lint cleanup where applicable and update plan status notes.

### 9l: Final M9 closeout tasks (next implementation slice)

1. **Host-viewer C loop endgültig app-agnostisch machen:**
  - Verify no remaining demo-specific gameplay mutation path is reachable in C main loop.
  - Keep only: pacing, input forwarding, presentation, metrics.
2. **Static-slot skip behavior implementieren/verifizieren (M9→M7 throughput bridge):**
  - Persist per-slot `has_animation` detection from traversal.
  - Skip erase + rasterize for static slots until next published slot snapshot.
  - Add wake/sleep policy: if all slots static, render thread blocks on slot-change signal.
3. **Rendered-state query API vervollständigen:**
  - Ensure slot/entity miss behavior is deterministic (`nil`/sentinel contract documented).
  - Verify timeline step/progress semantics for non-timeline fields and `loop=false`.
4. **Acceptance + regression gate for M9:**
  - Unit tests: static-slot detection, skip behavior, wakeup on publish.
  - Integration tests: Clojure-driven slot updates visibly drive scene without C gameplay writes.
  - Host run: stable FPS + reduced changed-slot/dirty-pixel metrics when slots are static.
5. **Execute 9k cleanup and freeze M9 contract:**
  - Remove dead compatibility code paths.
  - Update docs/comments to reflect final two-thread model and APIs.
  - Mark M9 as DONE only after tests/build pass.

### 9m: Checkable PR task list (files + symbols)

- **PR-1: Static-slot skip + wake/sleep policy**
  - Files:
    - `src/host_viewer_minifb.c`
    - `src/scene.c`
    - `src/scene.h`
    - `src/tests/test_vector_scene_graph.c`
  - Scope:
    - Persist per-slot animation detection (`has_animation`) from traversal to slot render decision.
    - Skip erase/rasterize/transfer for static slots until next slot publish.
    - If all slots static, block render thread on slot-change wakeup.
  - Acceptance:
    - Tests prove static slot remains untouched across frames.
    - Publish of one slot wakes render and only that slot gets re-rendered.
  - Done (2026-03-05):
    - `has_animation` detection is now persisted in `VgRenderSlotState` from scene traversal.
    - Render thread now ticks animated slots without snapshot republish; static-only state blocks on slot-change wakeup.
    - Added unit tests for `has_animation` tracking + forced animation tick without generation change.
    - Added blocking wakeup regression test: `wait_for_changes(UINT32_MAX)` resumes on single-slot publish with exact slot mask.
    - Added slot-level rerender regression test proving single-slot publish re-renders only that slot.
- **PR-2: Rendered-state query API finalize**
  - Files:
    - `src/rendered_state_snapshot.c`
    - `src/rendered_state_snapshot.h`
    - `src/builtins.c`
    - `src/tiny-clj.runtime.clj`
    - `src/tests/test_vector_scene_graph.c`
  - Symbols/APIs:
    - `tiny-clj.runtime/renderer-state`
    - `tiny-clj.runtime/renderer-timeline-step`
    - `tiny-clj.runtime/renderer-timeline-progress`
  - Scope:
    - Define deterministic miss semantics (`nil`/sentinel) and document in runtime API.
    - Verify `loop=false` and non-Timeline field behavior for step/progress queries.
  - Acceptance:
    - Unit tests cover miss paths + loop/non-loop semantics.
    - API docs in `tiny-clj.runtime.clj` align with behavior.
  - Done (2026-03-05):
    - Runtime docs now define deterministic `nil` miss semantics for all three query APIs.
    - Added unit tests for non-Timeline fields returning `nil` and `loop=false` end-clamp semantics in queried timeline progress.
- **PR-3: Host loop app-agnostic verification + cleanup**
  - Files:
    - `src/host_viewer_minifb.c`
    - `src/renderer_lifecycle.c`
    - `src/renderer_lifecycle.h`
    - `src/tests/test_vector_scene_graph.c`
  - Scope:
    - Ensure no demo-specific gameplay mutation path is reachable in C main loop.
    - Keep only lifecycle/pacing/presentation/input responsibilities in C host loop.
    - Remove obsolete migration helpers/flags discovered during cleanup.
  - Acceptance:
    - Integration tests confirm scene moves via Clojure slot updates only.
    - Runtime lifecycle tests still pass (`start-renderer!` / `stop-renderer!`).
  - Status (2026-03-05):
    - Host path reads live framebuffer directly (lock-free, no copy buffer) — faithful ESP32 SPI/I80 GRAM simulation.
    - Obsolete per-frame render completion wait/condvar path removed from host loop.
    - Lifecycle integration gate re-run completed (`test_vector_scene_graph/*renderer_lifecycle`*: 38 tests, 0 failures).
    - Integration proof covered by regression tests: scene motion data remains Timeline-driven from Clojure slot snapshots (`test_vector_scene_graph_host_viewer_demo_game_motion_is_timeline_driven`) and collision response is callback-routed from Clojure (`test_vector_scene_graph_host_viewer_demo_collision_callback_toggles_player_geometry_in_clojure`).
- **PR-4: End-of-M9 cleanup + documentation freeze**
  - Files:
    - `src/host_viewer_minifb.c`
    - `src/scene.c`
    - `src/rendered_state_snapshot.c`
    - `src/tiny-clj.runtime.clj`
    - `.cursor/plans/esp32_vector_scene_graph_engine.plan.md`
    - optional docs updates under `docs/` if needed
  - Scope:
    - Remove dead compatibility code paths and duplicate runtime paths.
    - Ensure one canonical path for slot publication, render consumption, lifecycle API.
    - Update plan status + docs to mark M9 DONE.
  - Acceptance:
    - Full relevant test suite passes.
    - M9 `Status` changed to DONE with final notes.
  - Done (2026-03-05):
    - Removed remaining legacy async compatibility republish path from host-viewer loop.
    - Finalized collision callback namespace split (`tiny-gfx.collision`) and removed collision callback API from `tiny-clj.runtime`.
    - Re-ran M9 regression gates (`test_vector_scene_graph/`*, `test_vector_scene_graph/*renderer_lifecycle`*, host-viewer demo/collision subsets).
- **PR-5: Clojure-configurable collision callback + collision-response extraction**
  - Files:
    - `src/tiny-gfx.host-viewer-demo.clj`
    - `src/tiny-gfx.collision.clj`
    - `src/host_viewer_minifb.c`
    - `src/viewer_legacy_collision.c`
    - `src/viewer_legacy_collision.h`
    - `src/tests/test_vector_scene_graph.c`
  - Scope:
    - Add Clojure API to configure the collision toggle callback function.
    - Remove hardcoded callback expression from C.
    - Keep per-frame collision detection policy in C (`viewer_legacy_collision` path).
    - Move demo collision response/scene mutation into Clojure callback path only.
    - Dispatch callback closures through scheduler/runloop ingress from C, without callback return-value coupling.
    - Delete obsolete C scene-mutation helpers after migration.
  - Acceptance:
    - Host demo still toggles player geometry on collisions.
    - Callback target is configurable from Clojure at runtime.
    - Collision detection remains in C; collision response is routed via Clojure callback.
    - C ignores callback return values; scene mutations are explicitly performed by Clojure code.
    - Regression tests cover callback reconfiguration + toggle behavior.
  - Done (2026-03-05):
    - Added collision callback API in `tiny-gfx.collision` (`set-collision-callback!`, `invoke-collision-callback!`) with deterministic nil/dispatch behavior.
    - Host viewer collision callback invocation now resolves/calls runtime dispatcher directly (no hardcoded expression string eval path).
    - Added regression coverage for callback reconfiguration and retained toggle behavior through C-driven collision detection.

### 9n: `tiny-gfx` API parity + Clojure documentation hardening (NEW)

Status: IN PROGRESS (started 2026-03-05; initial runtime exposure + scene-contract docs merged)

Goal:

- Expose the currently implemented vector-scene runtime capabilities through `tiny-gfx.`* namespaces.
- Keep `tiny-clj.runtime/`* compatibility, but provide `tiny-gfx`-first API surface + docs.
- Document Clojure-side API contracts where users author scene/collision/runtime code.

Scope (first slice):

- Add `tiny-gfx.runtime` namespace with documented direct aliases for:
  - `vector-scene-bench`
  - `start-renderer!` / `stop-renderer!`
  - `renderer-state`
  - `renderer-timeline-step`
  - `renderer-timeline-progress`
- Embed `/libs/tiny-gfx/runtime.clj` in `embedded_sources.c`.
- Add regression tests proving alias parity against existing runtime semantics.

Scope (second slice):

- Expand `tiny-gfx.scene` Clojure docs for record contracts actually consumed by C:
  - `Transform`, `Style`, node records, `Timeline`, `FrameScene`, `CollisionRule`, `CollisionEvent`
  - field semantics for `clip-rect`, `guard-px`, `erase-color`, and collision rule defaults
- Add contract notes for currently schema-only vs runtime-evaluated fields.

Acceptance:

- `require 'tiny-gfx.runtime` works in host/unit-test runtime.
- Wrapper APIs return same values/errors as the underlying `tiny-clj.runtime` functions.
- Clojure docs are present at API entry points used by scene authors.

Checklist:

- Add embedded `tiny-gfx.runtime` namespace and aliases.
- Add tests for alias parity (`unsupported` lifecycle path + rendered-state query nil path).
- Add/extend Clojure API docs in `tiny-gfx.scene` for consumed record contracts.

### Done when

- Host-viewer demo uses flat entity map architecture end-to-end:
Clojure `{id → Record}` in Atom → C resolves Timelines + interpolates → renders.
- Root entity is identified by symbol `root`.
- Periodic animations run via Timeline Records (no Clojure timers for visual cycling).
- No direct `ASSIGN` of computed positions from C into scene Records.
- Collision callback target for demo is configured from Clojure (not hardcoded in C).
- Per-frame collision detection remains in C; no direct demo-specific scene mutation remains in C host-viewer loop.
- Callback dispatch from C to Clojure is closure-based via scheduler/runloop ingress.
- Callback return values are not transferred back to C and are not used for scene updates.
- Scene updates from collision response happen explicitly in Clojure (`swap!`/`reset!`).
- Static slots are not erased/re-rendered/transferred until a new slot snapshot is published.
- M9 cleanup pass (9k) is complete and obsolete code paths are removed.
- One vertical slice runs the same scene model in simulator and on device.

## Milestone 10: Collision Contract + Scheduler Callback Bridge

Status: NEXT UP (Step 1: generalized Clojure API contract freeze)

Note:

- This milestone is engine-level collision infrastructure.
- Host demo gameplay remains Clojure-owned (including callback selection and scene mutation).
- Milestone objective: generalized Clojure-facing collision API (rule declaration + callback configuration + deterministic event contract), with per-frame collision detection executed in C.
- Callback execution model for this milestone: C dispatches configured closure callbacks through scheduler/runloop ingress; callback return values are ignored by C.

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
- Callback invocation semantics:
  - callback target is a configured Clojure closure
  - callback is dispatched via scheduler/runloop ingress (never executed inline on render thread)
  - C does not consume callback return values
  - any scene mutation must be performed explicitly by Clojure callback code (`swap!`/`reset!`)
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

1. Generalized Clojure API contract freeze (no runtime behavior change yet)
  - finalize collision-rule schema fields and defaults
  - finalize callback-configuration API shape (closure registration/selection from Clojure)
  - finalize callback-event payload fields and phase vocabulary
  - freeze no-return callback contract (C ignores callback return values)
  - document canonical pair-id behavior and latch semantics
  - add schema-level tests for rule decoding/default handling
2. Scheduler callback ingress (thread-safe boundary only)
  - implement thread-safe ingress queue (MPSC + FIFO)
  - add wakeup signaling and scheduler-thread drain API
  - add close/drain shutdown semantics
  - tests: FIFO ordering, concurrent producer safety, scheduler-thread-only closure execution
3. C collision engine baseline (without callback wiring enabled by default)
  - decode collision rules from snapshot
  - resolve object ids to render objects and compute overlap
  - maintain enter/stay/exit latch state per canonical pair key
  - tests: deterministic overlap transitions from synthetic snapshots
4. Bridge C collision events into scheduler ingress
  - enqueue callback payloads from C collision pass
  - drain and dispatch callback closures on scheduler thread (one-way dispatch, no return-value use)
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
  - no-return callback contract is fixed and documented (C ignores callback return values)
  - no runtime behavior change in renderer/scheduler yet
- Gate 2 (after step 2):
  - scheduler ingress queue API is available and thread-safe
  - callback closures still run exclusively on scheduler thread
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

- Start with Step 1 only (generalized Clojure API contract freeze):
  - add explicit rule/event schema docs in code comments and plan references
  - add explicit callback-configuration contract docs and tests
  - add schema-focused tests (defaults, disabled rules, phase-mask normalization)
  - postpone all scheduler/C runtime changes until Gate 1 passes

Step 1 checklist (next concrete tasks):

- Add explicit API docs for `tiny-gfx.collision` callback configuration semantics (set/clear/invoke, nil behavior, closure dispatch path, no-return contract).
- Add Clojure schema-level tests for collision-rule defaults and phase-mask normalization via `tiny-gfx.scene/normalize-collision-rule`.
- Add Clojure schema-level tests for disabled-rule handling contract (`:enabled false`) without runtime engine coupling.
- Add contract tests that callback return values are ignored by C (one-way dispatch contract).
- Keep renderer/scheduler runtime behavior unchanged while Gate 1 tests are established.

Implementation notes (2026-03-06):

- Added callback configuration contract docs in `tiny-gfx.collision` (set/clear/invoke semantics, closure dispatch path, no-return C contract).
- Added schema-focused collision contract tests in `test_gfx_collision_contract`:
  - defaults + phase-mask normalization
  - disabled-rule (`:enabled false`) behavior
  - callback set/clear/invoke shape + invalid non-fn rejection
  - demo callback mutates Clojure scene state explicitly
- Host-viewer callback bridge now dispatches via event-loop API (`event_loop_enqueue`/`event_loop_run_next`) and ignores callback return values in C.
- Added dedicated bridge-level regression (`test_gfx_collision_contract_runloop_dispatch_ignores_callback_return_value`) proving runloop dispatch executes callback side effects while callback return values remain non-contractual to C.
- Step-2 ingress foundation added in `event_loop`:
  - thread-safe callback ingress API (`event_loop_enqueue_ingress`, `event_loop_ingress_has_pending`)
  - runloop drains ingress queue before normal timer/task processing
  - host-viewer collision bridge now uses ingress API for callback dispatch handoff
  - regression coverage: `test_event_loop_latency/event_loop_ingress_enqueue_executes_on_run_next`
  - ingress close/reject semantics (`event_loop_ingress_close`, `event_loop_ingress_is_closed`) with drain-after-close behavior
  - concurrent producer safety regression (`test_event_loop_latency/event_loop_ingress_concurrent_producers_fifo_drain`)
  - bounded backpressure regression (`test_event_loop_latency/event_loop_ingress_backpressure_rejects_when_full_and_recovers_after_drain`)
  - runtime stats now expose ingress backpressure counters:
  `:event-loop-ingress-accepted-count`, `:event-loop-ingress-rejected-count`,
  `:event-loop-ingress-drained-count`, `:event-loop-ingress-high-watermark`,
  `:event-loop-ingress-pending-count`, `:event-loop-ingress-closed`

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
  - callback dispatch is one-way (C does not consume callback return values)
- Implement scheduler ingress thread-safety for collision callbacks:
  - add thread-safe callback queue + wakeup signaling
  - ensure scheduler-thread-only processing of queued callbacks
  - add queue close/drain behavior for clean shutdown
- Add host-side tests for collision determinism and callback behavior:
  - stable ids across snapshots
  - moved object triggers enter/exit transitions correctly
  - unchanged frame does not duplicate `:enter` events
  - enqueue/drain order remains FIFO under concurrent producer activity
  - scheduler thread alone executes callback closures (no cross-thread callback execution)
  - callback return values are ignored (no scene writeback contract to C)
- Current host-demo state (intermediate step):
  - obstacle world bounding box is already derived automatically from geometry + explicit transform application and cached across frames
  - collision hitbox is intentionally still set manually for gameplay tuning/stability
  - callback contract/events to tiny-clj scheduler remain part of this milestone TODO

Done when:

- Collision pairs are specified declaratively via stable ids in scene records.
- Collision callbacks are configurable from Clojure through one generalized API surface (not demo-specific hardcoded symbols).
- C collision checks run automatically from snapshot input using Clojure-declared pair specs and dispatch deterministic closure callbacks into the existing Clojure scheduler.
- Callback return values are ignored by C; scene updates are explicit Clojure-side mutations.

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
11. M7 ESP32 display backend integration (SPI/I80 slot windows)
12. M7b bandwidth optimization (entity-level dirty tracking + selective erase/redraw)
13. M9 game/menu/title integration
14. M10 collision contract + C-dispatched closure callback
15. M11 user guide (Markdown)
16. Optional later: explicit patch path + pointer-identity subtree reuse

Rule:

- Do not start M7 (ESP32 display backend) before M5 snapshot-slot rendering passes in host simulator.

## Risk Register

- Risk: Thick stroke rasterization consumes too much frame budget.
  - Mitigation: axis-aligned fast paths, simple default caps/joins, command caps.
- Risk: Simulator behavior diverges from ESP32 backend behavior.
  - Mitigation: shared render core, backend conformance tests, deterministic frame traces.
- Risk: Transform stack overhead in deep group trees.
  - Mitigation: shallow graph conventions, iterative traversal, compact matrix representation.
- Risk: SPI/I80 bandwidth limits visible complexity.
  - Mitigation: dirty-rectangle tracking (only changed pixels transferred), static-slot skip, span merging, DMA pipeline, adaptive interlacing fallback.
  - Reference: 240×240@16bit full frame = 115.200 bytes; at 40 MHz SPI ~23 ms/frame (max ~43 fps). Dirty-rect tracking typically reduces transfer to 40–60% → effective 60+ fps for most game scenes.
  - I80 parallel interface provides ~8× the bandwidth of SPI at same clock frequency, relaxing this constraint significantly.
- Risk: tiny-clj allocation spikes during snapshot rebuilds.
  - Mitigation: persistent data sharing, coarse scene slots, changed-slot-only rendering, optional compiled caches later.
- Risk: Slot rectangles appear non-overlapping logically but overlap in raster output due to stroke/text fringes.
  - Mitigation: conservative `clip-rect` guard bands, explicit slot background policy, test coverage for edge clipping.
- Risk: Float drift/non-determinism in long animations.
  - Mitigation: fixed-first decode/transform path aligned with `subjective-c` fractional bits (`CLJ_FIXED_FRAC_BITS = 13`), integer quantization only at raster boundaries.

## Milestone 11: User Guide (Markdown)

Status: TODO

Goal:

- Write a complete user-facing Markdown guide that documents the final engine API and workflows.
- Target audience: developers building scenes/gameplay in Clojure on top of the renderer.

Tasks:

- Create guide file (proposed path): `docs/USER_GUIDE_VECTOR_SCENE.md`.
- Document core model and lifecycle:
  - slot atoms and snapshot publication
  - renderer lifecycle (`start-renderer!`, `stop-renderer!`)
  - thread model (Tiny-RTOS/Clojure main thread, render thread, macOS UI detail)
- Document scene authoring:
  - flat entity maps (`{id -> Record}`)
  - root entity (`root` symbol)
  - groups and child-id references
  - changing grouping dynamically at runtime (re-parenting / child list updates)
- Document animation features with examples:
  - Timeline-based periodic animations
  - per-keyframe durations (uneven step times)
  - transform interpolation (AnimState-backed smooth motion)
  - querying current visual state (`renderer-state`, `renderer-timeline-step`, `renderer-timeline-progress`)
- Document shape and style updates:
  - changing geometry (`:pts`, tri vertices, text)
  - changing style/color/visibility
  - dynamic shape replacement patterns
- Document time-driven flows:
  - scheduler-driven updates
  - event/callback-driven updates
  - static-slot optimization behavior (when rerender happens / does not happen)
- Document collision features:
  - declaring collision rules in Clojure
  - callback configuration and payload contract
  - distinction between C collision detection and Clojure collision response
- Add end-to-end examples:
  - minimal scene
  - animated scene with mixed static/animated slots
  - collision-driven gameplay mutation
- Add troubleshooting section:
  - common mistakes (wrong IDs, missing `root`, stale snapshot assumptions)
  - debugging tips via rendered-state query APIs

Done when:

- `docs/USER_GUIDE_VECTOR_SCENE.md` exists and is linked from relevant docs/README entry points.
- Guide covers all major feature areas: animations, collision detection, time-driven updates, object grouping/re-grouping, shape changes.
- Guide examples are validated against current runtime API (no outdated symbols/contracts).


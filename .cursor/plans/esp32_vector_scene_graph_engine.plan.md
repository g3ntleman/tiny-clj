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
- Deterministic immediate rendering on SPI displays (ST7789 class) after host PoC is stable
- Thick-stroke primitives suitable for game, menu, and animated vector title screens

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

## Definition Of Done (project-level)

1. macOS simulator renders deterministic reference frames from the same scene contract used on ESP32.
2. Nested groups with inherited transforms render deterministically frame-to-frame.
3. Required primitives work: `Group`, `Line`, `Polyline`, `Rect`, `Tri`, `VText`.
4. Thick stroke (`width >= 1`) looks stable and readable on 320x240.
5. World movement is achievable by updating group transform fields only (e.g. `world.t.tx`).
6. Multiple scene slots with `clip-rect` can be rendered independently and unchanged slots are skipped.
7. SPI backend supports slot-scoped clipping + window/burst writes without requiring a full-screen redraw.
8. Optional-later delta paths (id-based patches and/or pointer-identity subtree reuse) are compatible with the same render core.

## Delivery Strategy (Stepwise)

Phase A (PoC on host):

- Build and validate scene model, transforms, rasterization, thick lines, and snapshot-slot render flow on macOS.
- Use deterministic frame dumps/checksums as regression gates.

Phase B (embedded integration):

- Reuse validated render core on ESP32 with SPI transport integration.
- Tune clipping/write strategy for SPI bandwidth limits.

## Milestone 0: Scene Contract + Record Schema

Status: PARTIAL (C scene structs exist; tiny-clj record schema + slot-index cache still TODO)

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
- Keep canonicalization/compiled C scene cache optional for later optimization.

Done when:

- Record schema and direct-render access contract are documented and testable.
- Nil/default/inheritance decoding behavior is documented and covered by schema-level tests.

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

Status: PARTIAL (functional and used by host demo; explicit cap/join semantics + perf gates still TODO)

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
- Keep advanced caps/joins optional for later:
  - `:square`, `:round`, miter/round joins

Done when:

- Thick lines are visually stable in simulator output and within host frame-time budget.

## Milestone 5: Snapshot Slot Update Path (PoC Gate 2, Patch Optional)

Status: TODO (architecture agreed; implementation pending, existing single-patch path remains optional support code)

Tasks:

- Define `FrameScene`/render-slot record contract:
  - `root`
  - `clip-rect`
  - `z`
  - `visible?`
  - `opaque?` / clear policy
  - optional guard pixels for conservative clipping
- Define snapshot publication protocol for render thread consumption:
  - atomically replace one `FrameScene` descriptor (or equivalent) without in-place mutation of the active snapshot
- Render multiple explicit scene roots/atoms in host runtime (e.g. `game`, `score`, `deco`).
- In render thread, track previous slot snapshot and only clear/render/send slot region when the slot snapshot or slot properties change.
- Render only the affected slot region (`clip-rect`), not the full framebuffer.
- If a slot moves, treat dirty region as `union(old_clip_rect, new_clip_rect)`.
- Validate non-overlapping slot convention with conservative bounds (stroke width / text fringe guard).
- Validate scrolling/parallax using separate scene slots or group transforms within a slot.
- Current state (optional support path): `vg_scene_apply_patch()` exists for single `id`-based patch application.
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

Done when:

- Scene renders on device without full-frame requirement and with pacing consistent with simulator logic.

## Milestone 8: Fixed-Point Transform Path (Optional, Recommended)

Status: PARTIAL (text transform path uses fixed-point; full renderer/backend switch still TODO)

Tasks:

- Add optional fixed-point transform core aligned with `subjective-c` numeric representation:
  - integer values remain `fixnum`
  - fractional fixed path uses `subjective-c` fixed payload fractional bits (`CLJ_FIXED_FRAC_BITS`)
- Keep tiny-clj API unchanged (record render adapter decodes numeric inputs into renderer fixed/integer math).
- Keep renderer hot path fixed-point/integer only.
- Maintain integer clipping and integer rasterization.
- Add compile-time switch for float vs fixed backend.
- Current state: text transform path already uses renderer fixed-point math and now shares fractional-bit constants with `subjective-c`; thick-line rasterization still uses float math.

Done when:

- Fixed-point mode is stable, deterministic, and measurably beneficial in hot paths.

## Milestone 9: Game/Menu/Title Integration

Status: TODO

Tasks:

- Validate scene reuse across:
  - gameplay HUD
  - menu UI
  - vector title animation
- Validate update path from tiny-clj game state -> frame scene slots (snapshots) -> renderer.
- Optionally validate patch-based hotpath updates for selected widgets/animations.
- Run long soak tests on host and ESP32 to check stability and memory behavior.

Done when:

- One vertical slice runs the same scene model in simulator and on device.

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
4. M3 baseline primitives (`width=1`)
5. M4 thick line support
6. M5 snapshot slot update path (`FrameScene` + `clip-rect` + changed-slot-only render)
7. M6 VText integration
8. M7 SPI backend integration (slot windows on SPI)
9. M9 game/menu/title integration
10. Optional later: explicit patch path + pointer-identity subtree reuse

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
  - Mitigation: optional fixed-point transform core aligned with `subjective-c` fixed fractional bits (currently `CLJ_FIXED_FRAC_BITS = 13`).

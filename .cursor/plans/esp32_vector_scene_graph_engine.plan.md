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
6. SPI backend supports clipping + window/burst writes without requiring a full-screen redraw.
7. Stable `id`-based patch updates work for transform/text/visibility/style changes.

## Delivery Strategy (Stepwise)

Phase A (PoC on host):

- Build and validate scene model, transforms, rasterization, thick lines, and patch flow on macOS.
- Use deterministic frame dumps/checksums as regression gates.

Phase B (embedded integration):

- Reuse validated render core on ESP32 with SPI transport integration.
- Tune clipping/write strategy for SPI bandwidth limits.

## Milestone 0: Scene Contract + Record Schema

Status: TODO

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
- Define canonicalization step from tiny-clj records into compact C render data.

Done when:

- Record schema and canonicalization contract are documented and testable.

## Milestone 1: macOS Host Simulator Skeleton (PoC Gate 1)

Status: TODO

Tasks:

- Add a host-side simulator target for 320x240 rendering.
- Implement offscreen RGB565 frame target in host memory.
- Implement host simulator as a driver/backend implementation under the same primitive/render API
  used by ESP32 (do not bypass primitive rasterizers with a separate host-only drawing path).
- Add deterministic frame export (PPM or raw dump) for golden testing.
- Add frame checksum utility to compare expected vs actual output.
- Ensure simulator consumes the same canonical scene data format as embedded.

Done when:

- A simple static scene renders on macOS and produces stable frame checksums across runs.

## Milestone 2: Transform System (SVG-like behavior)

Status: TODO

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

Status: TODO

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

Status: TODO

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

## Milestone 5: Record-Friendly Diff/Patch Path (PoC Gate 2)

Status: TODO

Tasks:

- Enforce stable `id` per node.
- Define minimal patch operations:
  - transform changed
  - text changed
  - visibility changed
  - style (`stroke`, `width`) changed
- Implement low-allocation patch apply path in host runtime.
- Add guardrails:
  - max patches per frame
  - fallback behavior on overflow
- Validate scrolling by patching one group transform (`world.t.tx`) only.
- Validate parallax via second group transform (`bg.t.tx = world.t.tx * k`).

Done when:

- Common animation and scrolling updates are expressible as small `id`-based patches.

## Milestone 6: VText (Vector/Stroke Font)

Status: TODO

Tasks:

- Define minimal stroke-font glyph representation.
- Implement `VText` draw path using line/polyline stroking.
- Support `scale`, `rot`, and inherited parent transforms.
- Support style-based stroke color and width.
- Validate animated/rotated title text ("flying text") use case in simulator.

Done when:

- Animated vector text remains readable and stable under transform updates.

## Milestone 7: ESP32 SPI Backend Integration (Post-PoC)

Status: TODO

Tasks:

- Integrate validated raster output with SPI transport model:
  - set window + burst writes
- Keep backend boundary identical between host and ESP32 so host can emulate the device driver layer
  while still exercising the same primitive rasterization pipeline.
- Keep full framebuffer optional (not mandatory).
- Implement minimal clipping/culling before draw submission.
- Add optional dirty-region mode switch.
- Add backend conformance checks:
  - same scene input should produce same primitive command sequence as simulator.

Done when:

- Scene renders on device without full-frame requirement and with pacing consistent with simulator logic.

## Milestone 8: Fixed-Point Transform Path (Optional, Recommended)

Status: TODO

Tasks:

- Add optional 16.16 fixed-point transform core.
- Keep tiny-clj API unchanged (canonicalize converts numeric inputs).
- Keep renderer hot path fixed-point/integer only.
- Maintain integer clipping and integer rasterization.
- Add compile-time switch for float vs fixed backend.

Done when:

- Fixed-point mode is stable, deterministic, and measurably beneficial in hot paths.

## Milestone 9: Game/Menu/Title Integration

Status: TODO

Tasks:

- Validate scene reuse across:
  - gameplay HUD
  - menu UI
  - vector title animation
- Validate update path from tiny-clj game state -> scene patches -> renderer.
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
6. M5 id-based patch path + scrolling/parallax PoC
7. M6 VText integration
8. M7 SPI backend integration
9. M9 game/menu/title integration

Rule:

- Do not start M7 (ESP32 SPI integration) before M5 passes in host simulator.

## Risk Register

- Risk: Thick stroke rasterization consumes too much frame budget.
  - Mitigation: axis-aligned fast paths, simple default caps/joins, command caps.
- Risk: Simulator behavior diverges from ESP32 backend behavior.
  - Mitigation: shared render core, backend conformance tests, deterministic frame traces.
- Risk: Transform stack overhead in deep group trees.
  - Mitigation: shallow graph conventions, iterative traversal, compact matrix representation.
- Risk: SPI bandwidth limits visible complexity.
  - Mitigation: clipping/culling, dirty-region mode, avoid pathological tiny draw commands.
- Risk: tiny-clj allocation spikes during patch generation.
  - Mitigation: stable IDs, bounded patch vectors, canonicalized compact render data.
- Risk: Float drift/non-determinism in long animations.
  - Mitigation: optional fixed-point transform core (16.16).

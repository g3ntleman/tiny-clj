# ESP32 Vector Scene Graph Engine – Implementation Result

## Context

This file summarizes the implementation outcome for the vector scene graph host/renderer workstream and records the final decisions after iterative visual tuning.

Base planning reference:
- `.cursor/plans/esp32_vector_scene_graph_engine.plan.md`

Implemented branch:
- `feature/esp32-vector-scene-graph-poc`

Primary implementation commit:
- `0964a3d` (`Add vector scene graph host viewer and arcade text font pipeline.`)

## Final Outcome

### Delivered

1. Shared vector scene graph renderer in C (`src/vector_scene_graph.c/.h`) with:
   - Group/line/polyline/rect/tri/text nodes
   - Transform composition and application
   - Style handling (`stroke`, visibility, optional background for AA path)
   - Scene patch updates

2. Host viewer pipeline on macOS using MiniFB:
   - `src/host_viewer_minifb.c`
   - `src/host_viewer_macos_menu.m/.h`
   - CMake integration for optional viewer target (`TINYCLJ_HOST_VIEWER`)
   - Resizable window and in-window FPS text rendering

3. Deterministic unit test coverage for scene graph functionality:
   - `src/tests/test_vector_scene_graph.c`
   - transform behavior, checksum determinism, thick lines, AA behavior, patch behavior, text-spacing/non-squashing regressions

4. Font data tooling and assets:
   - Hershey subset import script and generated header
   - Additional font conversion experiments were performed and reduced back to a clean final renderer state

### Final Font Decision

- Renderer is now returned to the Star-Wars-style `arcadefont` single-stroke glyph source for alphanumeric text (`A-Z`, `0-9`), because it produced the best visual result in this pipeline.
- README now documents this source and includes links to project and preview:
  - `https://github.com/coolbutuseless/arcadefont`
  - `https://raw.githubusercontent.com/coolbutuseless/arcadefont/master/man/figures/README-starwars-1.png`

### Font Conversion Path (implemented)

The active Star-Wars-style font is converted into renderer glyph segments as follows:

1. Source data:
   - `arcadefont` point/stroke definitions from `data-raw/create-arcade-font.R`
   - Glyphs are defined on a fixed 9x9 integer grid with per-glyph stroke lists.

2. Parsing:
   - Point strings are split into stroke runs (`:` separators).
   - Each stroke is expanded into ordered 2D points.

3. Coordinate normalization:
   - Input coordinates are mapped from font-local orientation to renderer text orientation.
   - Grid points are kept integer (no outline fill stage, no contour triangulation).

4. Segment generation:
   - Consecutive points in each stroke are converted to `GL(x1, y1, x2, y2)` line segments.
   - Alphanumeric glyph switch-cases (`A-Z`, `0-9`) are emitted into `draw_text_node`.

5. Runtime rendering:
   - Text transform is applied via a fixed-point path in the text renderer (fractional bits now aligned with `subjective-c` fixed payload constants, `CLJ_FIXED_FRAC_BITS`, currently 13).
   - Segments are rasterized with the renderer’s 1px/thick line path (including text-specific anti-dropout handling).

## Important Technical Finding

During evaluation of alternative fonts (including ST-01/FifteenTwenty), the key issue was confirmed:

- Outline OTF fonts rendered through this pipeline appear visually "fatter"/outlined because the pipeline consumes contour edges, not true centerlines.
- A correct centerline conversion would require an additional skeletonization/centerline extraction step.

This is a structural format mismatch (outline source vs. single-stroke renderer), not just a minor numeric precision issue.

## Validation Status

The following were repeatedly executed during finalization:

- `cmake --build build --target unit-tests`
- Focused unit tests in `test_vector_scene_graph_*`
- `cmake --build build-hostviewer --target vector-host-viewer`

Status: passing in the final Star-Wars-font cleanup state.

## Scope Notes

- Existing unrelated local modifications in `.cursor/plans/esp32_vector_scene_graph_engine.plan.md` were intentionally left untouched.
- The resulting implementation prioritizes renderer correctness/debuggability on host and visual consistency for low-resolution text in the current framebuffer pipeline.

## Recommended Next Steps

1. Keep `arcadefont` as baseline unless centerline extraction is introduced.
2. If ST-01-like aesthetics are still desired, prototype a dedicated OTF-outline-to-centerline conversion stage before quantization.
3. Keep host preview as canonical visual gate before ESP32 backend integration changes.

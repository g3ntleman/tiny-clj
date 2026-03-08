# Vector Scene Fixed-First Contract (Milestone 8)

Date: 2026-03-04

## Scope

This note captures the fixed-first decode/transform contract for `scene.c` and a small host-side decode+render benchmark slice used for Milestone 8 validation.

## Fixed-First Decode Contract

Renderer entry points:
- `vg_render_scene_record`
- `vg_render_scene_record_clipped`
- `vg_render_frame_slot_record_if_changed`

Numeric decode policy in `src/scene.c`:
- Transform and scale fields decode as fixed-point payload first (`CLJ_FIXED_FRAC_BITS`).
- Both `fixnum` and `fixed` values are accepted.
- `nil` yields deterministic typed defaults.
- No float conversion is used in decode/compose hot paths.

Quantization boundaries:
- Transform composition and point transformation remain fixed-point.
- Integer conversion is only done at raster boundaries (framebuffer/clipping/index math).
- No fixed->int->fixed ping-pong in compose/apply stages.

Flat-map scene support (Milestone 9 compatibility):
- `Scene.root`/`FrameScene.root` may be a flat entity map.
- Root node resolves from map key `root` (symbol).
- Group `children` entries are interpreted as IDs when child entries are not direct records.

## Host Decode+Render Micro Benchmark

Test:
- `test_vector_scene_graph_decode_render_host_micro_benchmark`
- Source: `src/tests/test_vector_scene_graph.c`

Command:

```bash
./build/unit-tests --verbose --test 'test_vector_scene_graph/*micro_benchmark*'
```

Current sample output (Debug build, macOS host):

```text
BENCH vector_scene_record/deco iterations=800 total_ms=29.047 per_frame_ms=0.036309 fps=27541.6
BENCH vector_scene_record/score iterations=800 total_ms=14.707 per_frame_ms=0.018384 fps=54395.9
BENCH vector_scene_record/game iterations=800 total_ms=57.608 per_frame_ms=0.072010 fps=13887.0
```

Interpretation:
- Decode+render cost is stable and very low for representative game demo scenes.
- The mixed game scene is the heaviest slice and remains well below 1 ms/frame on host.

Runtime API (host + ESP32):
- `tiny-clj.runtime/vector-scene-bench`
- Args: `(vector-scene-bench iterations warmup)` (both optional; defaults `800`, `40`)
- Returns a map with totals and per-frame microseconds for `deco`, `score`, and `game`.

Host command:

```bash
./build/tiny-clj-repl -e "(do (require 'tiny-clj.runtime) (tiny-clj.runtime/vector-scene-bench 800 40))"
```

Sample output (Debug host build):

```text
{:platform "macOS", :iterations 800, :warmup 40, :deco-total-ms 28, :score-total-ms 14, :game-total-ms 59, :deco-us-per-frame 35, :score-us-per-frame 17, :game-us-per-frame 73, :total-ms 101}
```

## Host vs ESP32 Comparison Status

- Host baseline: available via the benchmark above.
- ESP32 build path: verified on 2026-03-04 with `./build_idf.sh --no-move` (successful link + image generation).
- ESP32 run path: available via the same `tiny-clj.runtime/vector-scene-bench` function in UART REPL.
- ESP32 capture command sequence:

```bash
cd esp32-idf
idf.py -p <PORT> flash monitor
```

At the REPL prompt:

```clojure
(do
  (require 'tiny-clj.runtime)
  (tiny-clj.runtime/vector-scene-bench 800 40))
```

- ESP32 comparison numbers: pending device-side capture.
- Milestone 8 core requirement remains satisfied; ESP32 comparison is tracked as optional follow-up.

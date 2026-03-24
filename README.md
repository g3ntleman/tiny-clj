# Tiny-CLJ

An **embedded-first Clojure interpreter** for microcontrollers (ESP32) and desktop platforms (macOS, Linux). Written in pure C99/C11 for maximum portability and minimal resource usage.

## Status: usable Alpha-Version 0.5.

## Prerequisites

### Required Tools
- **C Compiler**: GCC 4.9+ or Clang 3.5+ (C99/C11 support)
- **CMake**: 3.10+ for build system
- **Make**: GNU Make or compatible
- **Git**: For version control and cloning

### Platform-Specific Requirements

#### macOS
```bash
# Install Xcode Command Line Tools
xcode-select --install

# Or install via Homebrew
brew install cmake
```


### Optional Tools
### Optional Tools

## Primary Objective
**Follow the Clojure language as good as possible.** Maximum compatibility with standard [Clojure](https://clojure.org) features, syntax, and behavior.

## Key Features

### Core Language Features
- **Basic UTF-8 Support:** Unicode character handling for international text
- **REPL Line Editing:** Interactive command-line editing with arrow keys (aka linereader)
- **Multi-line REPL Editor:** Multi-line input editing (requires terminal emulation / a real TTY)
- **Error Messages with Source References:** Detailed error reporting with line numbers and context
- **Persistent Collections:** Inefficient, partially implemented vectors, maps, and sequences
- **Clojure-Compatible:** Standard Clojure syntax (`*ns*`, `def`, `fn`, etc)

### Technical Features
- **Embedded Target:** ESP32 microcontrollers
- **Pure C99/C11:** No POSIX-only features for embedded compatibility
- **Manual Reference Counting:** Predictable memory behavior on embedded systems
- **Small Binary:** Optimized for embedded deployment

### Sound Engine
- **Piezo-Focused Playback:** Embedded-first sound runtime for piezo buzzers with matching desktop simulation on macOS
- **Composable Sound Pipeline:** `tiny-fx.trk1/prepare-track` compiles note/rest step lists into TRK1 bytes that `tiny-fx.sound` plays directly from memory
- **Musical Timing:** Supports millisecond durations plus musical values such as `:q`, `:e`, `:h`, dotted notes, and tempo-driven playback
- **Melody/Backing Roles:** Lead voice plus backing voices can be described declaratively with per-role channel and volume settings
- **Host Debugging Hooks:** Desktop status/debug helpers make it easy to validate audio behavior without ESP32 hardware

### Vector Graphics Engine
- **Scene Graph Primitives:** Groups, polylines, triangles, text, transforms, and styles can be composed into layered scenes
- **Timeline Animation:** Transform, style, and text fields support keyframe timelines with interpolation, looping, and easing
- **Slot-Based Renderer:** `FrameScene` slots provide z-ordering, clip rectangles, guard pixels, and efficient dirty-rect rerendering
- **Spatial Events:** Collision and proximity rules publish host/runtime events for gameplay and interaction logic
- **Runtime Introspection:** Renderer state and timeline progress can be queried from Clojure for debugging, tooling, and tests

## Quick Start

### Building

Tiny-CLJ uses a single `build/` directory for all build configurations. The build type is specified when configuring CMake:

```bash
# Debug Build (default, includes debugging symbols and memory profiling)
cmake -DCMAKE_BUILD_TYPE=Debug -B build
cmake --build build

# Release Build (optimized for performance)
cmake -DCMAKE_BUILD_TYPE=Release -B build
cmake --build build

# Embedded Build (ultra-compact embedded configuration)
cmake -DCMAKE_BUILD_TYPE=Embedded -B build
cmake --build build
```

**Note:** All executables are placed in the `build/` directory. The build type and configuration information is displayed when starting the REPL or running unit tests.

### ESP32 Binary Size

For size comparisons, use a **Release** (or Embedded) build. Debug builds are much larger.

```bash
./scripts/measure_esp32_size.sh build
```

### ESP-IDF (ESP32 toolchain) via git submodule

This repo pins **ESP-IDF v5.3.x** as a git submodule under `external/esp-idf` so contributors can reproduce the ESP32 toolchain setup.

```bash
# 1) Fetch submodules (required once after clone)
git submodule update --init --recursive external/esp-idf

# 2) Download ESP-IDF tools/toolchains into a repo-local directory (./external/)
./scripts/setup_esp_idf.sh

# 3) Activate ESP-IDF environment (adds xtensa-esp32-elf-* tools to PATH)
source ./scripts/esp_env.sh

# Verify tools
which xtensa-esp32-elf-gcc
which xtensa-esp32-elf-size
```

With the ESP-IDF environment active, you can run the flash-tree size accounting script:

```bash
external/flash-tree/scripts/size_report_esp32.sh
```

### Task: flash image (ESP-IDF app)

Preferred (build + flash in one go, uses repo-local ESP-IDF env):

```bash
./build_idf.sh --flash
```

If the build already exists in the centralized build directory, flash it via `idf.py`:

```bash
cd esp32-idf
source ../scripts/esp_env.sh
idf.py -B ../builds/esp32-idf/build -p PORT flash
```

Lowest-level alternative (use the generated flash arguments):

```bash
cd builds/esp32-idf/build
python -m esptool --chip esp32 -b 460800 --before default_reset --after hard_reset write_flash "@flash_args"
```

### Running

```bash
# REPL (shows build information at startup)
./build/tiny-clj

# Generic tiny-fx app bundle
open -n ./build/tiny-fx.app

# Unit Tests (shows build information at startup)
./build/unit-tests

# Examples
./build/tiny-clj --no-core -e "(+ 1 2)"
./build/tiny-clj -f program.clj
```

### Host Viewer Font

The vector game demo uses a monospace single-stroke "Star-Wars arcade" font
for uppercase letters and digits (`A-Z`, `0-9`). The glyph data is derived from
the open-source `arcadefont` project (MIT license):

- https://github.com/coolbutuseless/arcadefont
- https://raw.githubusercontent.com/coolbutuseless/arcadefont/master/man/figures/README-starwars-1.png

### Build Information

Build information (type, date, compiler, enabled features) is automatically displayed:
- **REPL**: When starting the REPL
- **Unit Tests**: When running unit tests

This helps identify which build configuration is currently active and how it was built.

## Architecture
- **Core Types:** `CljObject` with specific structs for vectors, lists, maps, strings, symbols, exceptions
- **Evaluation:** Simple list evaluation with C-implemented built-ins
- **Parser:** Tokenization and AST parsing into `CljObject` structures
- **Memory:** Manual reference counting with `retain`, `release`, and `autorelease` pools
- **REPL:** Supports `--no-core`, `-e/--eval`, and `-f/--file` for scripted evaluation

## Feature Flags

Feature flags are compile-time defines. All feature toggles are **positive** `*_ENABLED` macros (use `0`/`1`). `DEBUG` is separate.

Common flags:
- `META_ENABLED` (metadata support)
- `MEMORY_PROFILER_ENABLED` (compile the memory-profiler implementation)
- `MEMORY_PROFILING_ENABLED` (activate profiling hooks/macros)
- `LINE_EDITING_ENABLED` (REPL line editor)
- `STRING_FORMATTING_ENABLED` (formatted exceptions via `vsnprintf`)
- `ERROR_MESSAGES_ENABLED` (full error message strings)
- `COMPLEX_PARSING_ENABLED` (vector/map parsing helpers)
- `REPL_ENABLED` (REPL features; embedded builds typically set `0`)

Note: `ZOMBIE_ENABLED` is a debug aid for lifetime bugs (does not reflect real freeing).

## Deviations from Clojure

Tiny-CLJ aims to follow Clojure closely, but there are some intentional differences:

- **Compile-time macroexpansion argument cap:** during compile-time macroexpansion, Tiny-CLJ currently passes at most **20** arguments to the macro function; additional arguments are ignored. Clojure does not impose such a cap.

## Documentation
See `docs/` directory for detailed documentation:
- **`ROADMAP.md`** - Planned work and status
- **`DEVELOPMENT_INSIGHTS.md`** - API design and memory management
- **`MEMORY_POLICY.md`** - Memory management guidelines
- **`PERFORMANCE_GUIDE.md`** - Performance optimization
- **`ERROR_HANDLING_GUIDE.md`** - Exception handling
- **`TESTING_GUIDE.md`** - Unity test framework and debugging
- **`RELEASE_NOTES.md`** - Version history and changes
- **`RC-COW.md`** - Reference counting and copy-on-write implementation
- **`MEMORY_PROFILER.md`** - Memory profiling and leak detection
- **`SOUND_DSL.md`** - Sound step-sequencer DSL and melody/backing options
- **`SOUND_USER_GUIDE.md`** - Practical guide for composing piezo-friendly music
- **`VECTOR_SCENE_FIXED_FIRST.md`** - Fixed-point vector scene/rendering notes

## Contributing
- Keep the core small and clean
- Favor small, focused PRs with tests and benchmark updates
- Follow English documentation standards

# Tiny-CLJ

An **embedded-first Clojure interpreter** for microcontrollers (ESP32) and desktop platforms (macOS, Linux). Written in pure C99/C11 for maximum portability and minimal resource usage.

## Status: Not usable, yet. Alpha. Embedded target not functional, yet.

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
- **Small Binary:** Target <150KB for embedded deployment

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

# Embedded Build (ultra-compact for <150KB target)
cmake -DCMAKE_BUILD_TYPE=Embedded -B build
cmake --build build
```

**Note:** All executables are placed in the `build/` directory. The build type and configuration information is displayed when starting the REPL or running unit tests.

### ESP32 Binary Size

For size comparisons, use a **Release** (or Embedded) build. Debug builds are much larger.

```bash
./scripts/measure_esp32_size.sh build-release
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
./build/tiny-clj-repl

# Unit Tests (shows build information at startup)
./build/unit-tests

# Examples
./build/tiny-clj-repl --no-core -e "(+ 1 2)"
./build/tiny-clj-repl -f program.clj
```

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

Compile-time toggles are mostly **positive** `*_ENABLED` macros (`0`/`1`). Root **CMake options** (see `CMakeLists.txt`) set the defaults; each executable may override `target_compile_definitions` (e.g. ESP32 vs desktop REPL). `DEBUG` and `PROFILING_ENABLED` are separate from the `*_ENABLED` pattern.

**CMake options** (names match the preprocessor macros they drive, except `ASAN_ENABLED`, which enables AddressSanitizer for the `unit-tests` target):

- `META_ENABLED` — preprocessor `META_ENABLED` for shared libs and tests (default ON).
- `MEMORY_PROFILING_ENABLED` — preprocessor `MEMORY_PROFILING_ENABLED` (default OFF).
- `ZOMBIE_ENABLED` — when ON, defines `ZOMBIE_ENABLED` (default OFF).
- `TINY_FX_ENABLED` — global `TINY_FX_ENABLED` (default ON).
- `MINIFB_METAL_ENABLED` — MiniFB uses the Metal backend on macOS (default OFF).
- `ASAN_ENABLED` — AddressSanitizer for the `unit-tests` target (default OFF).

**Preprocessor macros** set per target include: `META_ENABLED`, `MEMORY_PROFILING_ENABLED`, `LINE_EDITING_ENABLED`, `REPL_ENABLED`, `STRING_FORMATTING_ENABLED`, `ERROR_MESSAGES_ENABLED`, `DEBUG_SYMBOLS_ENABLED`, `COMPLEX_PARSING_ENABLED`, `ESP32_BUILD`, and `PROFILING_ENABLED` (profiling executable). `memory_profiler.h` gates the `MEMORY_PROFILER_*` tracking macros on `MEMORY_PROFILING_ENABLED`.

Note: `ZOMBIE_ENABLED` is a debug aid for lifetime bugs (does not reflect real freeing).

## Deviations from Clojure

Tiny-CLJ aims to follow Clojure closely, but there are some intentional differences:

- **Compile-time macroexpansion argument cap:** during compile-time macroexpansion, Tiny-CLJ currently passes at most **20** arguments to the macro function; additional arguments are ignored. Clojure does not impose such a cap.

- **`long` and JVM numeric coercions:** Tiny-CLJ does not bind JVM-only helpers such as `long` (or the wider `clojure.lang.Numbers`-style coercion surface). Arithmetic uses fixnums; treating `long` as a callable var fails with an unresolved-symbol error like any other unbound name. This is intentional and not planned to match the JVM here.

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

## Contributing
- Keep the core small and clean
- Favor small, focused PRs with tests and benchmark updates
- Follow English documentation standards
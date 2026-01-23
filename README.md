# Tiny-CLJ

An **embedded-first Clojure interpreter** for microcontrollers (ESP32) and desktop platforms (macOS, Linux). Written in pure C99/C11 for maximum portability and minimal resource usage.

## Status: Not usable, yet. Pre-alpha. Embedded target not functional, yet.

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
- **ESP32CubeIDE**: For ESP32 development and debugging
- **OpenOCD**: For ESP32 flashing and debugging
- **GDB**: For debugging (usually included with compiler toolchain)

## Primary Objective
**Follow the Clojure language as good as possible.** Maximum compatibility with standard [Clojure](https://clojure.org) features, syntax, and behavior.

## Key Features

### Core Language Features
- **Basic UTF-8 Support:** Unicode character handling for international text
- **REPL Line Editing:** Interactive command-line editing with arrow keys (aka linereader)
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

## Contributing
- Keep the core small and clean
- Favor small, focused PRs with tests and benchmark updates
- Follow English documentation standards
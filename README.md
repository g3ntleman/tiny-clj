# Tiny-CLJ

An **embedded-first Clojure interpreter** for microcontrollers (ESP32, ARM Cortex-M) and desktop platforms (macOS, Linux). Written in pure C99/C11 for maximum portability and minimal resource usage.

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
- **Embedded Target:** ESP32, ESP32, ARM Cortex-M microcontrollers
- **Pure C99/C11:** No POSIX-only features for embedded compatibility
- **Manual Reference Counting:** Predictable memory behavior on embedded systems
- **Small Binary:** Target <150KB for embedded deployment

## Quick Start

```bash
# Build
cmake . && make -j

# REPL
./tiny-clj-repl

# Tests
ctest --output-on-failure

# Examples
./tiny-clj-repl --no-core -e "(+ 1 2)"
./tiny-clj-repl -f program.clj
```

## Architecture
- **Core Types:** `CljObject` with specific structs for vectors, lists, maps, strings, symbols, exceptions
- **Evaluation:** Simple list evaluation with C-implemented built-ins
- **Parser:** Tokenization and AST parsing into `CljObject` structures
- **Memory:** Manual reference counting with `retain`, `release`, and `autorelease` pools
- **REPL:** Supports `--no-core`, `-e/--eval`, and `-f/--file` for scripted evaluation

## Documentation
See `Guides/` directory for detailed documentation:
- **`ROADMAP.md`** - Planned work and status
- **`DEVELOPMENT_INSIGHTS.md`** - API design and memory management
- **`MEMORY_POLICY.md`** - Memory management guidelines
- **`PERFORMANCE_GUIDE.md`** - Performance optimization
- **`ERROR_HANDLING_GUIDE.md`** - Exception handling
- **`TESTING_GUIDE.md`** - Unity test framework and debugging
- **`RELEASE_NOTES.md`** - Version history and changes

Additional documentation:
- **`RC-COW.md`** - Reference counting and copy-on-write implementation
- **`docs/MEMORY_PROFILER.md`** - Memory profiling and leak detection

## Contributing
- Keep the core small and clean
- Favor small, focused PRs with tests and benchmark updates
- Follow English documentation standards
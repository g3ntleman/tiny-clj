# Tiny-CLJ

An **embedded-first Clojure interpreter** for microcontrollers (STM32, ARM Cortex-M) and desktop platforms (macOS, Linux). Written in pure C99/C11 for maximum portability and minimal resource usage.

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

#### Linux (Ubuntu/Debian)
```bash
sudo apt update
sudo apt install build-essential cmake git
```

#### Linux (CentOS/RHEL/Fedora)
```bash
# CentOS/RHEL
sudo yum groupinstall "Development Tools"
sudo yum install cmake git

# Fedora
sudo dnf groupinstall "Development Tools"
sudo dnf install cmake git
```

#### Windows (MinGW/MSYS2)
```bash
# Install MSYS2, then:
pacman -S mingw-w64-x86_64-gcc mingw-w64-x86_64-cmake mingw-w64-x86_64-make
```

### Optional Tools
- **STM32CubeIDE**: For STM32 development and debugging
- **OpenOCD**: For STM32 flashing and debugging
- **GDB**: For debugging (usually included with compiler toolchain)

## Primary Objective
**Follow the Clojure language as good as possible.** Maximum compatibility with standard Clojure features, syntax, and behavior.

## Key Features
- **Embedded Target:** STM32, ESP32, ARM Cortex-M microcontrollers
- **Pure C99/C11:** No POSIX-only features for embedded compatibility
- **Manual Reference Counting:** Predictable memory behavior on embedded systems
- **Small Binary:** Target <60KB for embedded deployment
- **Clojure-Compatible:** Standard Clojure syntax (`*ns*`, `def`, `fn`, etc)

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

## Contributing
- Keep the core small and clean
- Favor small, focused PRs with tests and benchmark updates
- Follow English documentation standards
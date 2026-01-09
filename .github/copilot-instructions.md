**Project Overview**
- **Purpose:** Tiny-CLJ is an embedded-first Clojure interpreter written in C99/C11, targeting ESP32 and desktop platforms. See [README.md](../README.md) for high-level goals and architecture notes.
- **Big Picture:** Core evaluation and types are implemented around a central CljObject abstraction; parser/tokenizer produce CljObject trees; evaluation uses C-implemented builtins and manual reference counting (retain/release/autorelease).

**Build & Test (quick commands)**
- **Build local REPL:** `make tiny-clj-repl` or use the workspace task `build-repl` (runs `make tiny-clj-repl`). Built artifacts appear in the `build/` directory.
- **Full build:** `make all` or workspace task `build-all`.
- **Unit tests:** `make unit-tests` then run `./build/unit-tests` or use workspace task `build-tests` (runs `make unit-tests`). See docs/TESTING_GUIDE.md and the `src/tests/` directory.
- **Unit test options:**
  - `./build/unit-tests --help` - Show all available options
  - `./build/unit-tests --list` - List all available tests
  - `./build/unit-tests --test <name>` - Run specific test(s) (supports wildcards like `test_map/*`)
  - `./build/unit-tests --quiet` - Reduce memory leak reporting for cleaner output
  - `./build/unit-tests --memory-summary` - Show memory profiler summary after tests
- **Embedded targets:** `make tiny-clj-esp32` and `make tiny-clj-esp32-repl`. ESP32 workflow may require OpenOCD/GDB and ESP toolchain; see docs and tiny-clj-esp32 directories.

**Key Files & Directories**
- **README.md:** high-level architecture, build examples, and primary objectives. See [README.md](../README.md).
- **CMakeLists.txt / Makefile:** primary build configuration (CMake-generated). See [CMakeLists.txt](../CMakeLists.txt) and [Makefile](../Makefile).
- **docs/**: important guidance (MEMORY_POLICY.md, RC-COW.md, TESTING_GUIDE.md, PERFORMANCE_GUIDE.md). See [docs](../docs) for memory and profiling details.
- **src/tests/**: unit-test harness and tests using the Unity framework.
- **tiny-clj-esp32, tiny-clj-esp32-repl:** embedded-specific code and targets.

**Project-Specific Patterns & Conventions**
- **Manual reference counting:** Objects are managed with explicit retain/release and autorelease pools. Search for retain/release to find ownership rules; follow existing patterns rather than introducing GC-like idioms.
- **CljObject-centered API:** Most subsystems accept and return CljObject instances; prefer using helper constructors for types (strings, vectors, maps) instead of manually populating structs.
- **Limited macro expansion args:** At compile-time macroexpansion the project caps passed args at 20 — code that expects variable-arity macros should account for this limitation.
- **Single build directory:** All builds use a `build/` directory configured via CMake. Don't scatter artifacts into source tree.
- **Commit messages:** Prefer ~20 lines (subject + short context + key bullets + test/verification line).

**Integration Points & External Dependencies**
- **ESP32 toolchain:** ESP-specific targets assume cross-compiler and flashing/debugging tools (OpenOCD/GDB). See tiny-clj-esp32 docs.
- **Profiling & Memory tools:** Project includes memory-profiler helpers and scripts in docs; consult MEMORY_PROFILER.md and PERFORMANCE_GUIDE.md when adding instrumentation.

**Debugging Tips**
- **Start REPL for quick checks:** `./build/tiny-clj-repl` prints build info and state — useful to validate build flags and feature enables.
- **Unit tests with verbose output:** Run `./build/unit-tests` after build; tests include diagnostic output and memory checks per docs/TESTING_GUIDE.md.
- **Preserve small, focused changes:** The codebase favors minimal, well-scoped PRs with tests and benchmark updates.

**When to call out maintainers**
- If a change touches manual reference counting, RC-COW behavior, or core evaluation (parser/evaluator) ask for review from maintainers — these are high-risk areas.

If any section is unclear or you want more examples (specific functions, typical CljObject constructors, or test patterns), tell me which area to expand.

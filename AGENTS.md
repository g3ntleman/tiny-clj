# AGENTS.md

## LSP CLI for Symbol Work

For symbol lookup and renames in this repo, check the direct LSP tool first, not just `grep`.

- Tool: `scripts/lsp_cli.py`
- Purpose: fast `definition`, `references`, `callsites`, `summary`, `rename` via stdio LSP
- Presets: `--server clangd|typescript|vtsls|kotlin|pyright` (or `--server-command` for custom servers)
- Requirement: `clangd` in `PATH` and `build/compile_commands.json`
- Help/usage: `scripts/lsp_cli.py --help` and `scripts/lsp_cli.py <command> --help`

Beispiele:

```bash
scripts/lsp_cli.py definition src/repl_history_backend.c:279:6
scripts/lsp_cli.py --server typescript definition web/src/app.ts:42:7
scripts/lsp_cli.py --server kotlin summary app/src/main/kotlin/Foo.kt:12:9
scripts/lsp_cli.py --server pyright definition tools/foo.py:10:3
scripts/lsp_cli.py summary src/repl_history_backend.c:279:6
scripts/lsp_cli.py callsites src/repl_history_backend.c:279:6
scripts/lsp_cli.py references src/repl_history_backend.c:279:6 --include-declaration
scripts/lsp_cli.py rename src/repl_history_backend.c:279:6 neuer_name
```

Note: `callsites`, `incoming-calls`, and `outgoing-calls` use LSP Call Hierarchy when available; depending on the `clangd` version this may be partially unavailable.

Keywords for discoverability: `lsp`, `clangd`, `typescript-language-server`, `vtsls`, `kotlin-language-server`, `pyright-langserver`, `definition`, `references`, `callsites`, `summary`, `rename`, `grep`.

## Memory Policy (mandatory)

All functions must follow `MEMORY_POLICY.md`.

- Public/API/`native_*`/`eval_*` functions: follow `MEMORY_POLICY.md` directly.
- Internal exceptions only if explicitly marked as `make_*` or `*_owned` (clear owned contract).
- No implicit or branch-dependent ownership contracts.

## Debugging (mandatory)

Capture bug hypotheses as regression unit tests first, before (or alongside) the fix.

- Formulate the hypothesis so the test isolates the suspected mechanism (minimal repro, not just a large end-to-end test).

## Commit Messages

Do not add this trailer to commits:

`Co-authored-by: Cursor <cursoragent@cursor.com>`

## Planning documents (project-specific)

- New or updated **feature/architecture plans** belong in the workspace folder **`Plans/`** at the repository root (versioned in git), e.g. `Plans/my-topic.plan.md`. If a plan is created under `.cursor/plans/`, **move** it to `Plans/` (do not duplicate; single canonical path).
- See `.cursor/rules/plans-workspace-location.mdc` and `.cursor/rules/plan-cleanup-step.mdc` (cleanup todos).

## Documentation Preference (project-specific)

- Document C APIs in the corresponding `.c` files.
- Use Doxygen-style C comments (`/** ... */`) with `@brief`, `@param`, and `@return` for API/function docs.
- Keep headers focused on declarations/contracts, with only minimal inline commentary when needed.

## Cursor Cloud specific instructions

### Linux build (Cloud Agent environment)

The project is macOS/ESP32-primary. Cloud Agent VMs run Linux. Key differences:

- **Platform source**: `src/platform_linux.c` is the Linux platform layer (equivalent to `src/platform_macos.c`). CMakeLists.txt selects the correct file via `TINYCLJ_PLATFORM_SOURCE`.
- **Compiler**: Use GCC (`CC=gcc cmake ...`) because the embedded Clojure sources use C++ raw string literals (`R"DELIM(...)DELIM"`), which GCC accepts as a GNU extension in C99/C11 mode. Clang in C mode does not support raw string literals.
- **Math library**: Linux targets link `-lm` explicitly (handled in CMakeLists.txt via `if(NOT APPLE)` guards).
- **libstdc++ symlink**: The `libstdc++.so` linker symlink at `/usr/lib/x86_64-linux-gnu/libstdc++.so` may be missing. If CMake configuration fails with `cannot find -lstdc++`, create it: `sudo ln -sf /usr/lib/gcc/x86_64-linux-gnu/13/libstdc++.so /usr/lib/x86_64-linux-gnu/libstdc++.so`.

### Build commands

```bash
git submodule update --init --recursive external/unity
CC=gcc cmake -DCMAKE_BUILD_TYPE=Debug -B build
cmake --build build --target unit-tests -j$(nproc)
cmake --build build --target tiny-clj -j$(nproc)
```

### Running

- **Tests**: `./build/unit-tests` (445+ tests). Use `--test "group/*"` to run specific groups.
- **REPL**: `./build/tiny-clj` for interactive mode, `./build/tiny-clj -e '(+ 1 2)'` for eval.
- **Known issue**: `test_file_io/tiny_clj_kv_supports_large_values` crashes with `malloc_consolidate` on Linux (pre-existing, not caused by the platform layer).

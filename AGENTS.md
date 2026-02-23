# AGENTS.md

## LSP CLI for Symbol Work

For symbol lookup and renames in this repo, check the direct LSP tool first, not just `grep`.

- Tool: `scripts/lsp_cli.py`
- Purpose: fast `definition`, `references`, `callsites`, `summary`, `rename` via stdio LSP
- Presets: `--server clangd|typescript|vtsls|kotlin` (or `--server-command` for custom servers)
- Requirement: `clangd` in `PATH` and `build/compile_commands.json`
- Help/usage: `scripts/lsp_cli.py --help` and `scripts/lsp_cli.py <command> --help`

Beispiele:

```bash
scripts/lsp_cli.py definition src/repl_history_backend.c:279:6
scripts/lsp_cli.py --server typescript definition web/src/app.ts:42:7
scripts/lsp_cli.py --server kotlin summary app/src/main/kotlin/Foo.kt:12:9
scripts/lsp_cli.py summary src/repl_history_backend.c:279:6
scripts/lsp_cli.py callsites src/repl_history_backend.c:279:6
scripts/lsp_cli.py references src/repl_history_backend.c:279:6 --include-declaration
scripts/lsp_cli.py rename src/repl_history_backend.c:279:6 neuer_name
```

Note: `callsites`, `incoming-calls`, and `outgoing-calls` use LSP Call Hierarchy when available; depending on the `clangd` version this may be partially unavailable.

Keywords for discoverability: `lsp`, `clangd`, `typescript-language-server`, `vtsls`, `kotlin-language-server`, `definition`, `references`, `callsites`, `summary`, `rename`, `grep`.

## Memory Policy (mandatory)

All functions must follow `MEMORY_POLICY.md`.

- Public/API/`native_*`/`eval_*` functions: follow `MEMORY_POLICY.md` directly.
- Internal exceptions only if explicitly marked as `make_*` or `*_owned` (clear owned contract).
- No implicit or branch-dependent ownership contracts.

## Debugging (mandatory)

Capture bug hypotheses as regression unit tests first, before (or alongside) the fix.

- Formulate the hypothesis so the test isolates the suspected mechanism (minimal repro, not just a large end-to-end test).

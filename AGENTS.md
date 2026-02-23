# AGENTS.md

## LSP CLI (clangd) für Symbolarbeit

Für Symbolsuche und Umbenennungen in diesem Repo zuerst das direkte LSP-Tool prüfen, nicht nur `grep`.

- Tool: `scripts/lsp_cli.py`
- Zweck: schnelle `definition`, `references`, `callsites`, `summary`, `rename`
- Voraussetzung: `clangd` im `PATH` und `build/compile_commands.json`
- Hilfe/Usage: `scripts/lsp_cli.py --help` sowie `scripts/lsp_cli.py <command> --help`

Beispiele:

```bash
scripts/lsp_cli.py definition src/repl_history_backend.c:279:6
scripts/lsp_cli.py summary src/repl_history_backend.c:279:6
scripts/lsp_cli.py callsites src/repl_history_backend.c:279:6
scripts/lsp_cli.py references src/repl_history_backend.c:279:6 --include-declaration
scripts/lsp_cli.py rename src/repl_history_backend.c:279:6 neuer_name
```

Hinweis: `callsites`, `incoming-calls` und `outgoing-calls` nutzen nach Möglichkeit LSP Call Hierarchy; je nach `clangd`-Version kann das teilweise fehlen.

Keywords für Auffindbarkeit: `lsp`, `clangd`, `definition`, `references`, `callsites`, `summary`, `rename`, `grep`.

## Memory Policy (verbindlich)

Alle Funktionen sollen sich an `MEMORY_POLICY.md` halten.

- Public/API-/`native_*`-/`eval_*`-Funktionen: `MEMORY_POLICY.md` direkt einhalten.
- Interne Ausnahmen nur explizit als `make_*` oder `*_owned` (klarer Owned-Contract).
- Keine impliziten/branchabhängigen Ownership-Contracts.

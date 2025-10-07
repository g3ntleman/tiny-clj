# Cursor/VSCode Build Configuration

## Problem

Wenn `make` fehlschlägt (z.B. test-integration kaputt), kann die REPL nicht gestartet werden, weil der `build` Task alle Targets baut.

## Lösung

### Separater REPL-Build-Task

**File:** `.vscode/tasks.json`

```json
{
  "label": "build-repl",
  "type": "shell",
  "command": "make tiny-clj-repl",
  "options": { "cwd": "${workspaceFolder}" },
  "problemMatcher": ["$gcc"],
  "group": "build"
}
```

### Launch-Configs verwenden build-repl

**File:** `.vscode/launch.json`

Alle REPL-Konfigurationen nutzen jetzt:
```json
"preLaunchTask": "build-repl"
```

## Vorteile

✅ **REPL baut isoliert** - Nur `tiny-clj-repl` Target  
✅ **Unabhängig von Tests** - Test-Fehler blockieren REPL nicht  
✅ **Schneller** - Nur 1 Target statt 4  
✅ **Zuverlässig** - REPL startet immer

## Tasks

| Task | Command | Zweck |
|------|---------|-------|
| `build` | `make` | Alle 4 Targets (default) |
| `build-repl` | `make tiny-clj-repl` | Nur REPL (isoliert) |
| `ctest` | `ctest --output-on-failure` | Tests ausführen |

## Anwendung

Die Änderungen sind bereits in den Dateien:
- `.vscode/tasks.json` 
- `.vscode/launch.json`

Diese Dateien werden normalerweise nicht committed (`.gitignore`),  
sind aber Teil der Projekt-Dokumentation.

**Bei neuem Checkout:** Einfach Cursor öffnen, die Konfiguration  
wird automatisch erkannt.

---

**Status:** ✅ Implementiert  
**Getestet:** ✅ REPL-Build funktioniert isoliert

# Evaluator Performance Optimization Plan

## Aktuelle Situation (nach Quick-Wins)

| System | ms/iter | vs tiny-clj |
|--------|---------|-------------|
| Clojure/JVM | 0.060 | 52x schneller |
| ClojureScript | 0.168 | 18.6x schneller |
| Python3 | 0.914 | 3.4x schneller |
| **tiny-clj** | **3.124** | - |

## Profiling-Erkenntnisse

| Funktion | CPU % | Problem |
|----------|-------|---------|
| `eval_list` | 25% | Viele serielle if-Checks, TAG-Calls |
| `as_list` | 16% | Typ-Checks (optimiert mit #ifdef DEBUG) |
| `eval_arg_from_expr_with_context` | 10% | Mehrere Pfade, AUTORELEASE overhead |
| `eval_numeric_comparison` | 7% | (optimiert mit Fixnum-Fast-Path) |

## Implementierte Optimierungen

### 1. Quick-Wins ✅
- Redundanten `SYM_NIL` Check entfernt
- `get_closure_env(ctx)` nur aufrufen wenn `ctx != NULL`
- **Ergebnis: ~8% schneller**

### 2. as_list Zero-Overhead ✅
- In Release-Builds: reiner Cast ohne Typ-Check
- Debug-Builds behalten volle Typ-Sicherheit

### 3. O(n) statt O(n²) Listen-Traversierung ✅
- `eval_arg_with_context` Schleifen durch direktes Traversieren ersetzt

### 4. Fixnum Fast-Paths ✅
- Arithmetik: direkte Berechnung ohne `apply_arith_op`
- Vergleiche: direkte Vergleiche ohne Float-Konvertierung

## Geplante Optimierungen

### 5. Lazy as_symbol ⏳
**Problem:** `as_symbol(expr)` wird aufgerufen, auch wenn der Wert nie verwendet wird.

**Loesung:** `as_symbol` erst aufrufen wenn benoetigt.

### 6. Inline frame_lookup ⏳
**Problem:** `frame_lookup` ist ein Funktionsaufruf fuer jeden Parameter-Zugriff.

**Loesung:** Inline-Makro fuer den Hot-Path.

### 7. TAG-Caching ⏳
**Problem:** `TAG(op)` wird mehrfach aufgerufen.

**Loesung:** TAG einmal am Anfang cachen.

### 8. Special-Form Erkennung (Pointer-Range) ✅
**Problem:** `is_special_symbol()` sollte O(1) sein (kein lineares Scannen).

**Loesung:** Special-Forms werden ueber stabile Pointer-Identitaet erkannt (kontiguierliches Special-Symbol-Array + Pointer-Range-Check). Kein zusaetzliches Flag-Bit noetig.

## Erwartete Gesamt-Verbesserung

| Optimierung | Impact |
|-------------|--------|
| Quick-Wins | ~8% ✅ |
| Lazy as_symbol | ~2-3% |
| Inline frame_lookup | ~3-5% |
| TAG-Caching | ~2-3% |
| Special-Form Erkennung | ✅ |
| **Gesamt** | **~15-20%** |

## Langfristige Optimierungen

- **Bytecode-Compiler**: AST -> Bytecode -> VM
- **JIT-Compilation**: Haeufig aufgerufene Funktionen nativ kompilieren
- **Tail-Call Trampoline**: Echtes TCO ohne Stack-Verbrauch

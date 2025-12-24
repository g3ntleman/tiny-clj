# Keyword-Semantik Analyse: Tiny-Clj vs. Clojure/JVM

## Clojure Keyword-Semantik (Referenz)

### Unterstützte Keyword-Formen:

1. **Unqualifiziert**: `:keyword`
   - Einfaches Keyword ohne Namespace
   - Beispiel: `:done`, `:active`, `:error`

2. **Explizit qualifiziert**: `:namespace/keyword`
   - Keyword mit explizitem Namespace
   - Beispiel: `:clojure.core/map`, `:user/test`

3. **Auto-qualifiziert (aktueller Namespace)**: `::keyword`
   - Wird automatisch mit dem aktuellen Namespace qualifiziert
   - Beispiel: Im Namespace `user` wird `::test` zu `:user/test`

4. **Auto-qualifiziert (Alias)**: `::alias/keyword`
   - Wird mit dem Namespace qualifiziert, der durch den Alias referenziert wird
   - Beispiel: `(require '[clojure.string :as str])` → `::str/trim` wird zu `:clojure.string/trim`

### Clojure-Funktionen:

- `qualified-keyword?` - Prüft, ob ein Keyword qualifiziert ist
- `namespace` - Gibt den Namespace eines Keywords zurück (oder `nil`)

## Aktuelle Tiny-Clj Implementierung

### Parser (`src/parser.c`):

**Zeile 697-702**: Keyword-Präfix wird erkannt:
```c
// Handle keyword prefix
if (reader_peek_char(reader) == ':') {
  buffer[pos++] = reader_next(reader);
  if (reader_peek_char(reader) == ':')
    buffer[pos++] = reader_next(reader);
}
```

**Problem**: `::` wird nur als Teil des Buffers behandelt, aber nicht speziell verarbeitet.

**Zeile 761-778**: Namespace-qualifizierte Symbole werden erkannt:
```c
// Check for namespace-qualified symbol: namespace/symbol or alias/symbol
if (slash_pos > 0 && slash_pos < pos - 1) {
  // Split buffer at '/': namespace/alias and symbol
  buffer[slash_pos] = '\0';
  const char *ns_str = buffer;
  const char *symbol_str = buffer + slash_pos + 1;
  // ...
}
```

**Problem**: Diese Logik funktioniert für `:namespace/keyword`, aber nicht für `::keyword`.

### Fehlende Funktionalität:

1. **`::keyword` Auto-Qualifizierung**: 
   - `parse_symbol` hat keinen Zugriff auf `EvalState *st` (Signatur: `static ID parse_symbol(Reader *reader)`)
   - Kann daher nicht den aktuellen Namespace ermitteln
   - `::keyword` wird als `::keyword` gespeichert (nicht qualifiziert)

2. **`::alias/keyword` Auto-Qualifizierung**:
   - Keine Behandlung für Alias-Auflösung bei `::alias/keyword`
   - Wird als `::alias/keyword` gespeichert (nicht korrekt qualifiziert)

3. **Keyword-Erkennung**:
   - `IS_KEYWORD` prüft nur `sym->cname[0] == ':'` (Zeile 28 in `src/symbol.h`)
   - Unterscheidet nicht zwischen qualifizierten und unqualifizierten Keywords

## Empfohlene Änderungen

### 1. `parse_symbol` Signatur ändern:

```c
static ID parse_symbol(Reader *reader, EvalState *st);
```

### 2. `::keyword` Auto-Qualifizierung implementieren:

```c
// Nach Zeile 701: Prüfe ob :: erkannt wurde
bool auto_qualify = (buffer[0] == ':' && buffer[1] == ':');

if (auto_qualify && !slash_pos) {
  // ::keyword - qualifiziere mit aktuellem Namespace
  if (st && st->current_ns && st->current_ns->name) {
    const char *current_ns_name = st->current_ns->name->cname;
    // Entferne :: vom Anfang
    const char *keyword_name = buffer + 2;
    // Erstelle qualifiziertes Keyword: :current-ns/keyword-name
    CljSymbol *ns_name_sym = intern_symbol_global(current_ns_name);
    CljSymbol *kw = intern_symbol(ns_name_sym, keyword_name);
    return AUTORELEASE(kw);
  }
}
```

### 3. `::alias/keyword` Auto-Qualifizierung implementieren:

```c
if (auto_qualify && slash_pos > 0) {
  // ::alias/keyword - qualifiziere mit Alias-Namespace
  // Split bei '/'
  buffer[slash_pos] = '\0';
  const char *alias_str = buffer + 2; // Skip ::
  const char *keyword_name = buffer + slash_pos + 1;
  
  if (st && st->current_ns && st->current_ns->aliases) {
    CljSymbol *alias_sym = intern_symbol_global(alias_str);
    CljObject *resolved_ns = ns_get_alias(st->current_ns, (CljObject*)alias_sym);
    if (resolved_ns && TAG(resolved_ns) == CLJ_SYMBOL) {
      CljSymbol *ns_name_sym = as_symbol(resolved_ns);
      CljSymbol *kw = intern_symbol(ns_name_sym, keyword_name);
      return AUTORELEASE(kw);
    }
  }
}
```

### 4. Alle Aufrufe von `parse_symbol` aktualisieren:

- Zeile 272: `parse_symbol(reader)` → `parse_symbol(reader, st)`
- Zeile 280: `parse_symbol(reader)` → `parse_symbol(reader, st)`

## Tests erforderlich

1. `::keyword` wird mit aktuellem Namespace qualifiziert
2. `::alias/keyword` wird mit Alias-Namespace qualifiziert
3. `:namespace/keyword` funktioniert weiterhin
4. `:keyword` funktioniert weiterhin (unqualifiziert)





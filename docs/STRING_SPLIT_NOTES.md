# Strings.c Funktionsgruppen-Analyse

## Übersicht
Diese Datei dokumentiert die Aufteilung von `subjective-c/strings.c` in reine String-Funktionen (bleiben in subjective-c) und Tiny-Clj-spezifische Funktionen (werden auf bestehende Dateien verteilt).

## Reine String-Funktionen (bleiben in subjective-c/strings.c)

Diese Funktionen benötigen nur `CljString` und generische Runtime-Funktionen:

### String-Erstellung
- `make_string_like()` - statisch, interne Helper-Funktion
- `make_string_impl()` - statisch, interne Helper-Funktion  
- `make_clj_string()` - öffentlich, String-Erstellung
- `make_string()` - öffentlich, String-Erstellung
- `make_string_buffer()` - öffentlich, Buffer-Erstellung
- `string_empty_singleton` - Singleton-Variable

### String-Escape (reine String-Operationen)
- `escape_string_calc_length()` - statisch, berechnet Länge mit Escape-Zeichen
- `escape_string_write()` - statisch, schreibt String mit Escape-Zeichen

### Benötigte Includes (nur für Core)
- `<stdlib.h>`, `<string.h>`, `<stdbool.h>`, `<stdio.h>`, `<assert.h>`, `<stdint.h>`
- `"object.h"` - für CljObject/CljString
- `"strings.h"` - Header-Definition
- `"memory.h"` - für `alloc()`
- `"types.h"` - für `SINGLETON_RC`

## Tiny-Clj-spezifische Funktionen (werden verschoben)

### Symbol-Token-Erzeugung → `src/symbol.c`
- `make_symbol_token()` - erzeugt CljSymbolToken (benötigt symbol_token.h)

### Special-Form-Verwaltung → `src/symbol.c`
- `g_print_special_forms_as_tags` - globale Variable
- `g_special_form_names[]` - globale Variable
- `g_special_form_count` - globale Variable
- `g_special_forms_initialized` - globale Variable
- `strings_set_special_form_rendering()` - öffentlich
- `strings_get_special_form_rendering()` - öffentlich
- `strings_register_special_form_internal()` - statisch
- `strings_register_special_form()` - öffentlich
- `strings_clear_special_forms()` - öffentlich
- `ensure_special_forms_initialized()` - statisch
- `is_special_symbol()` - statisch, benötigt CljSymbol

### Objekt-zu-String-Konvertierung → `src/object.c`
- `to_string_calc_length()` - statisch, rekursiv für alle Objekttypen
- `to_string_build_string()` - statisch, rekursiv für alle Objekttypen
- `to_string()` - öffentlich
- `to_string_with_escape()` - öffentlich
- `make_string_description()` - öffentlich
- `print_str()` - öffentlich

Diese Funktionen benötigen:
- `CljSymbol`, `CljNamespace`, `CljVector`, `CljList`, `CljPersistentMap`, `CljFunction`, `CljSeq`, `CljAtom`, `CljByteArray`, `CLJException`
- `namespace.h`, `symbol.h`, `vector.h`, `list.h`, `map.h`, `function.h`, `seq.h`, `atom.h`, `byte_array.h`, `exception.h`
- `value.h`, `runtime.h`, `kv_macros.h`

## Abhängigkeiten

### subjective-c/strings.c (nach Trennung)
**Includes:**
- Standard-C-Header (stdlib, string, stdbool, stdio, assert, stdint)
- `object.h` - CljObject/CljString Definition
- `strings.h` - Header-Definition
- `memory.h` - alloc()
- `types.h` - SINGLETON_RC

**Keine Abhängigkeiten mehr auf:**
- `symbol_token.h` ❌
- `namespace.h` ❌
- `symbol.h` ❌
- `vector.h` ❌
- `list.h` ❌
- `map.h` ❌
- `function.h` ❌
- `seq.h` ❌
- `exception.h` ❌
- `atom.h` ❌
- `byte_array.h` ❌
- `value.h` ❌
- `runtime.h` ❌
- `kv_macros.h` ❌

## Verwendungsstellen

### make_string / make_clj_string / make_string_buffer
- Wird in vielen Dateien verwendet für String-Erstellung
- Bleibt in subjective-c

### to_string / make_string_description / print_str
- Wird in builtins.c, eval.c, repl.c, tests verwendet
- Wird nach object.c verschoben

### strings_set_special_form_rendering / strings_get_special_form_rendering
- Wird in builtins.c verwendet
- Wird nach symbol.c verschoben

### strings_register_special_form / strings_clear_special_forms
- Wird in symbol.c und tests verwendet
- Wird nach symbol.c verschoben

### make_symbol_token
- Wird in parser.c verwendet
- Wird nach symbol.c verschoben

## Speicher-Optimierungen für Embedded

- `string_empty_singleton` als statisches Singleton (keine Heap-Allokation)
- `escape_string_calc_length` und `escape_string_write` verwenden Stack-Variablen
- Keine dynamischen Allokationen in Escape-Funktionen
- `make_string_like` verwendet `alloc()` direkt (Memory-Policy-konform)









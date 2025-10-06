# Error Handling Guide

## Übersicht

Das tiny-clj Projekt verwendet ein konsistentes Error-Handling-System mit zwei Hauptfunktionen:

1. **`throw_exception()`** - Für einfache Fehlermeldungen ohne Formatierung
2. **`throw_exception_formatted()`** - Für Fehlermeldungen mit printf-Style Formatierung

## Verwendung

### `throw_exception()` - Einfache Fehlermeldungen

Verwende `throw_exception()` für statische Fehlermeldungen ohne dynamische Parameter:

```c
// ✅ Korrekt - statische Nachricht
throw_exception("TypeError", "Attempt to call non-function value", NULL, 0, 0);

// ✅ Korrekt - statische Nachricht mit Datei/Zeile
throw_exception("ArityError", "Arity mismatch in function call", __FILE__, __LINE__, 0);
```

### `throw_exception_formatted()` - Formatierte Fehlermeldungen

Verwende `throw_exception_formatted()` nur wenn Format-String-Parameter vorhanden sind:

```c
// ✅ Korrekt - mit Format-String-Parametern
throw_exception_formatted("ArithmeticException", __FILE__, __LINE__, 0,
        "Invalid arguments for %s: expected two integers, got %s and %s", 
        arith_errors[op], a ? type_name(a->type) : "nil", b ? type_name(b->type) : "nil");

// ✅ Korrekt - mit numerischen Parametern
throw_exception_formatted("ArithmeticException", __FILE__, __LINE__, 0,
        "Division by zero: %d / %d", a->as.i, b->as.i);

// ✅ Korrekt - mit Pointer- und Typ-Informationen
throw_exception_formatted("DoubleFreeError", __FILE__, __LINE__, 0,
        "Double free detected! Object %p (type=%d, rc=%d) was freed twice. "
        "This indicates a memory management bug.", v, v->type, v->rc);
```

## Fehlerbeispiele

### ❌ Falsch - `throw_exception_formatted()` ohne Parameter

```c
// ❌ Falsch - keine Format-String-Parameter
throw_exception_formatted("TypeError", __FILE__, __LINE__, 0,
        "Attempt to call non-function value");
```

**Korrekt:**
```c
// ✅ Korrekt - einfache statische Nachricht
throw_exception("TypeError", "Attempt to call non-function value", NULL, 0, 0);
```

### ❌ Falsch - `throw_exception()` mit dynamischen Werten

```c
// ❌ Falsch - dynamische Werte ohne Formatierung
char message[256];
snprintf(message, sizeof(message), "Division by zero: %d / %d", a->as.i, b->as.i);
throw_exception("ArithmeticException", message, NULL, 0, 0);
```

**Korrekt:**
```c
// ✅ Korrekt - mit Format-String-Parametern
throw_exception_formatted("ArithmeticException", __FILE__, __LINE__, 0,
        "Division by zero: %d / %d", a->as.i, b->as.i);
```

## Vorteile der neuen Convenience-Funktion

### 1. **Buffer-Overflow-Schutz**
```c
// Automatische Truncation-Erkennung mit 512 Bytes Buffer
if (result >= (int)sizeof(message)) {
    message[sizeof(message)-1] = '\0';
}
```

### 2. **Vereinfachte API**
```c
// Vorher: Manueller snprintf + throw_exception
char message[256];
snprintf(message, sizeof(message), "Error: %s", error_msg);
throw_exception("RuntimeException", message, __FILE__, __LINE__, 0);

// Nachher: Ein Aufruf
throw_exception_formatted("RuntimeException", __FILE__, __LINE__, 0,
        "Error: %s", error_msg);
```

### 3. **NULL-Type für generische Exceptions**
```c
// type kann NULL sein für generische RuntimeException
throw_exception_formatted(NULL, __FILE__, __LINE__, 0,
        "Unexpected error: %s", error_msg);
```

## Best Practices

1. **Verwende `throw_exception_formatted()` nur bei Format-String-Parametern**
2. **Verwende `throw_exception()` für statische Nachrichten**
3. **Dokumentiere Exception-Types in Doxygen-Kommentaren**
4. **Verwende aussagekräftige Fehlermeldungen mit Kontext**
5. **Nutze `__FILE__` und `__LINE__` für bessere Debugging-Informationen**

## Exception-Types

- **`ArithmeticException`** - Mathematische Operationen
- **`TypeError`** - Falsche Datentypen
- **`ArityError`** - Falsche Anzahl Parameter
- **`IOError`** - Datei-/Eingabe-/Ausgabe-Fehler
- **`DoubleFreeError`** - Memory Management
- **`AutoreleasePoolError`** - Autorelease Pool
- **`AssertionError`** - Assertion-Fehler
- **`RuntimeException`** - Generische Laufzeitfehler

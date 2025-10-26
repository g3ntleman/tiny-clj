# Error Handling Guide

## Übersicht

Das tiny-clj Projekt verwendet ein robustes Exception-Handling-System inspiriert von Objective-C:

1. **TRY/CATCH/END_TRY Macros** - Strukturiertes Exception-Handling mit Auto-Release
2. **`throw_exception()`** - Für einfache Fehlermeldungen ohne Formatierung
3. **`throw_exception_formatted()`** - Für Fehlermeldungen mit printf-Style Formatierung

---

## TRY/CATCH/END_TRY System

### Grundlegende Verwendung

```c
TRY {
    risky_code();
    may_throw_exception();
} CATCH(ex) {
    // Handle exception
    fprintf(stderr, "Error: %s\n", ex->message);
    // Exception wird automatisch released - kein manuelles release_exception() nötig!
} END_TRY
```

### Features

1. **Auto-Release**: Exceptions werden automatisch freigegeben am Ende des CATCH-Blocks
2. **Verschachtelung**: Unbegrenzt verschachtelte TRY/CATCH Blöcke möglich
3. **Re-throw**: `throw_exception()` im CATCH-Block wirft an äußeren Handler
4. **Exception Stack**: Implementiert als Linked List in `EvalState->exception_stack`
5. **Pure C99**: Keine Platform-spezifischen Features
6. **Embedded-friendly**: Stack-basiert, keine Heap-Allokationen

### Verschachtelte Exception-Handling

```c
TRY {
    outer_code();
    
    TRY {
        inner_code();
        throw_exception("InnerError", "Something failed", __FILE__, __LINE__, 0);
    } CATCH(ex) {
        printf("Inner exception caught: %s\n", ex->message);
        // Handle or re-throw
    } END_TRY
    
    more_outer_code();
} CATCH(ex) {
    printf("Outer exception: %s\n", ex->message);
} END_TRY
```

### Re-throw Pattern

```c
TRY {
    TRY {
        dangerous_operation();
    } CATCH(ex) {
        // Log and re-throw with more context
        printf("Inner caught: %s\n", ex->message);
        throw_exception("WrapperException", "Operation failed in inner context", 
                       __FILE__, __LINE__, 0);
    } END_TRY
} CATCH(ex) {
    // Outer handler catches re-thrown exception
    printf("Final handler: %s\n", ex->message);
} END_TRY
```

### Wichtige Regeln

⚠️ **Einschränkungen:**
- Kein `break`, `continue`, oder `return` im TRY-Block verwenden!
- Bei Bedarf: Wrapper-Funktion verwenden

```c
// ❌ Falsch - return im TRY-Block
TRY {
    if (condition) return NULL;  // Überspringt Cleanup!
} CATCH(ex) {
    handle(ex);
} END_TRY

// ✅ Korrekt - Wrapper-Funktion
static CljObject* helper_with_return(EvalState *st) {
    TRY {
        if (condition) return NULL;  // OK in nested function
    } CATCH(ex) {
        handle(ex);
        return NULL;
    } END_TRY
    return result;
}
```

### Implementierungs-Details

**ExceptionHandler Struktur:**
```c
typedef struct ExceptionHandler {
    jmp_buf jump_state;          // Jump target für longjmp
    struct ExceptionHandler *next;  // Vorheriger Handler (Stack)
    CLJException *exception;     // Gefangene Exception
} ExceptionHandler;
```

**EvalState Integration:**
```c
typedef struct EvalState {
    // ...
    struct ExceptionHandler *exception_stack;  // Handler Stack
} EvalState;
```

**Makro-Expansion:**
```c
TRY {
    code();
} CATCH(ex) {
    handle(ex);
} END_TRY

// Expandiert zu:
{
    ExceptionHandler _h = {.next = st->exception_stack};
    st->exception_stack = &_h;  // Push auf Stack
    
    if (setjmp(_h.jump_state) == 0) {
        code();                 // TRY-Block
        st->exception_stack = _h.next;  // Pop (normal exit)
    } else {
        CLJException *ex = _h.exception;
        st->exception_stack = _h.next;  // Pop (exception exit)
        if (ex) {
            handle(ex);         // CATCH-Block (user code)
            release_exception(ex);  // Auto-release
        }
    }
}
```

**Warum END_TRY notwendig ist:**

Das äußere `{` vom TRY muss geschlossen werden. Ohne END_TRY würde re-throw im CATCH den Handler nicht korrekt vom Stack poppen, was zu Endlosschleifen führt. Die END_TRY Syntax folgt Objective-C (NS_ENDHANDLER) und vielen anderen C Exception-Handling Libraries.

---

## Exception Throwing

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

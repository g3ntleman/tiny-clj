# Analyse: Test-Isolation und gegenseitige Beeinflussung

## Übersicht
Diese Analyse untersucht, wie sich Tests gegenseitig beeinflussen können und welche globalen Zustände zwischen Tests persistieren.

## Kritische Probleme

### 1. **User Namespace wird zwischen Tests geteilt** ⚠️ KRITISCH

**Problem:**
- `g_runtime.ns_registry` wird in `runtime_init()` NICHT zurückgesetzt
- `ns_get_or_create("user", NULL)` sucht zuerst in der Registry
- Wenn Test A den "user" Namespace erstellt, wird dieser in der Registry gespeichert
- Test B bekommt dann den GLEICHEN "user" Namespace wie Test A
- **Folge**: Variablen/Funktionen, die in Test A definiert wurden, sind auch in Test B sichtbar!

**Code-Stelle:**
```c
// src/namespace.c:20-33
CljNamespace* ns_get_or_create(const char *name, const char *file) {
    // First, look for an existing namespace
    CljNamespace *cur = (CljNamespace*)g_runtime.ns_registry;
    while (cur) {
        if (cur->name) {
            CljSymbol *name_sym = as_symbol(cur->name);
            if (name_sym && strcmp(name_sym->name, name) == 0) {
                return cur;  // ❌ Gibt existierenden Namespace zurück!
            }
        }
        cur = cur->next;
    }
    // ... erstellt neuen Namespace nur wenn nicht gefunden
}
```

**Beispiel:**
```clojure
// Test A
(defn add [a b] (+ a b))  ; Definiert 'add' im "user" Namespace

// Test B (läuft nach Test A)
(add 1 2)  ; ❌ Funktioniert, obwohl 'add' nicht in Test B definiert wurde!
```

**Lösung:**
- User Namespace sollte in `setUp()` zurückgesetzt oder neu erstellt werden
- Oder: `g_runtime.ns_registry` sollte in `runtime_init()` zurückgesetzt werden (außer clojure.core)

### 2. **Symbol Resolution Cache wird nicht zurückgesetzt** ⚠️ KRITISCH

**Problem:**
- `g_resolve_cache` ist eine statische Variable in `namespace.c`
- Wird nie zurückgesetzt zwischen Tests
- Kann zu falschen Cache-Treffern führen

**Code-Stelle:**
```c
// src/namespace.c:18
static CljObject *g_resolve_cache = NULL;
```

**Beispiel:**
```clojure
// Test A
(defn test-fn [] 42)
(test-fn)  ; Cache speichert: test-fn -> Funktion

// Test B (läuft nach Test A)
(test-fn)  ; ❌ Cache gibt alte Funktion zurück, obwohl test-fn nicht definiert ist!
```

**Lösung:**
- `g_resolve_cache` sollte in `setUp()` zurückgesetzt werden
- Oder: Cache sollte pro Namespace sein, nicht global

### 3. **Namespace Registry wird nicht zurückgesetzt** ⚠️ WICHTIG

**Problem:**
- `g_runtime.ns_registry` wird in `runtime_init()` NICHT zurückgesetzt
- Namespaces, die in Test A erstellt wurden, sind auch in Test B sichtbar
- Nur `clojure.core` sollte persistieren

**Code-Stelle:**
```c
// src/runtime.c:21-37
void runtime_init(void) {
    void *preserved_cache = g_runtime.clojure_core_cache;
    void *preserved_symbol_table = g_runtime.symbol_table;
    
    memset(&g_runtime, 0, sizeof(TinyClJRuntime));  // ❌ Setzt ns_registry auf NULL
    // ...
    g_runtime.clojure_core_cache = preserved_cache;
    g_runtime.symbol_table = preserved_symbol_table;
    // ❌ ns_registry wird NICHT wiederhergestellt, aber clojure.core ist noch in der Registry!
}
```

**Lösung:**
- `ns_registry` sollte in `runtime_init()` zurückgesetzt werden
- Nur `clojure.core` Namespace sollte erhalten bleiben

## Nicht-kritische Probleme

### 4. **clojure.core Cache wird erhalten** ✅ KORREKT

**Status:** Bewusst so implementiert - clojure.core sollte zwischen Tests persistieren

### 5. **Symbol Table wird erhalten** ✅ KORREKT

**Status:** Bewusst so implementiert - Symbol-Interning sollte konsistent sein

## Empfohlene Lösungen

### Lösung 1: User Namespace in setUp() zurücksetzen

```c
void setUp(void) {
    // ... existing code ...
    
    // CRITICAL: Reset user namespace between tests
    // This ensures test isolation - each test starts with a clean user namespace
    CljNamespace *user_ns = ns_find("user");
    if (user_ns && user_ns != (CljNamespace*)g_runtime.clojure_core_cache) {
        // Clear user namespace mappings (but keep the namespace itself)
        if (user_ns->mappings) {
            RELEASE((CljObject*)user_ns->mappings);
            user_ns->mappings = (CljObject*)make_map(16);
        }
    }
    
    // Reset symbol resolution cache
    if (g_resolve_cache) {
        RELEASE((CljObject*)g_resolve_cache);
        g_resolve_cache = NULL;
    }
    
    // ... rest of setUp ...
}
```

### Lösung 2: Namespace Registry in runtime_init() zurücksetzen

```c
void runtime_init(void) {
    void *preserved_cache = g_runtime.clojure_core_cache;
    void *preserved_symbol_table = g_runtime.symbol_table;
    
    // Preserve clojure.core namespace in registry
    CljNamespace *clojure_core = (CljNamespace*)preserved_cache;
    
    memset(&g_runtime, 0, sizeof(TinyClJRuntime));
    g_runtime.pool_stack_top = -1;
    g_runtime.builtins_registered = false;
    
    // Restore cache and symbol table
    g_runtime.clojure_core_cache = preserved_cache;
    g_runtime.symbol_table = preserved_symbol_table;
    
    // CRITICAL: Reset namespace registry, but keep clojure.core
    if (clojure_core) {
        // Re-register clojure.core in registry
        clojure_core->next = NULL;
        g_runtime.ns_registry = (void*)clojure_core;
    } else {
        g_runtime.ns_registry = NULL;
    }
}
```

### Lösung 3: Symbol Resolution Cache zurücksetzen

```c
// In namespace.c - Funktion zum Zurücksetzen des Caches
void ns_reset_resolve_cache(void) {
    if (g_resolve_cache) {
        RELEASE((CljObject*)g_resolve_cache);
        g_resolve_cache = NULL;
    }
}

// In unity_test_runner.c setUp()
void setUp(void) {
    // ... existing code ...
    
    // Reset symbol resolution cache
    ns_reset_resolve_cache();
    
    // ... rest of setUp ...
}
```

## Test-Beispiele für Isolation

### Test 1: User Namespace Isolation
```c
TEST(test_user_namespace_isolation) {
    // Test A: Definiere Funktion
    eval_string("(defn test-fn [] 42)", g_test_eval_state);
    CljValue result = eval_string("(test-fn)", g_test_eval_state);
    TEST_ASSERT_EQUAL_INT(42, as_fixnum(result));
}

TEST(test_user_namespace_isolation_2) {
    // Test B: Funktion sollte NICHT existieren
    CljValue result = eval_string("(test-fn)", g_test_eval_state);
    // ❌ Sollte fehlschlagen, funktioniert aber aktuell!
    TEST_ASSERT_NULL(result);  // Sollte NULL sein, ist aber 42
}
```

### Test 2: Symbol Resolution Cache Isolation
```c
TEST(test_resolve_cache_isolation) {
    // Test A: Definiere Symbol
    eval_string("(def test-var 123)", g_test_eval_state);
    CljValue result = eval_string("test-var", g_test_eval_state);
    TEST_ASSERT_EQUAL_INT(123, as_fixnum(result));
}

TEST(test_resolve_cache_isolation_2) {
    // Test B: Symbol sollte NICHT existieren
    CljValue result = eval_string("test-var", g_test_eval_state);
    // ❌ Cache könnte alten Wert zurückgeben!
    TEST_ASSERT_NULL(result);
}
```

## Zusammenfassung

**Kritische Probleme:**
1. ✅ User Namespace wird zwischen Tests geteilt
2. ✅ Symbol Resolution Cache wird nicht zurückgesetzt
3. ✅ Namespace Registry wird nicht zurückgesetzt

**Empfohlene Priorität:**
1. **Hoch**: User Namespace zurücksetzen (Lösung 1)
2. **Hoch**: Symbol Resolution Cache zurücksetzen (Lösung 3)
3. **Mittel**: Namespace Registry zurücksetzen (Lösung 2)


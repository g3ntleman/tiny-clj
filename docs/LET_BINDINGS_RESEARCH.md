# Recherche: Let-Bindungen in verschachtelten Kontexten

## Zusammenfassung

Diese Dokumentation beschreibt, wie `let`-Bindungen in Lisp-Implementierungen, insbesondere Clojure und ClojureScript, in verschachtelten Kontexten aufgelöst werden.

## Kernkonzepte

### 1. Lexikalisches Scoping (Lexical Scoping)

Clojure und ClojureScript verwenden **lexikalisches Scoping** für `let`-Bindungen. Das bedeutet:

- Bindungen sind nur innerhalb des `let`-Blocks sichtbar
- Die Sichtbarkeit wird zur Compile-Zeit bestimmt (basierend auf der Code-Struktur)
- Verschachtelte `let`-Blöcke erweitern die äußere Umgebung

### 2. Umgebungsauflösung (Environment Resolution)

Die Auflösung von Symbolen in verschachtelten Kontexten erfolgt durch eine **Umgebungskette** (Environment Chain):

```
Global Namespace
    ↓
Function Parameters (closure_env)
    ↓
Outer let bindings
    ↓
Inner let bindings
    ↓
Current expression
```

#### Auflösungsreihenfolge:

1. **Lokale Bindungen** (innerste `let`-Umgebung)
2. **Äußere `let`-Bindungen** (verschachtelte Kontexte)
3. **Funktionsparameter** (closure environment)
4. **Namespace-Mappings** (clojure.core, etc.)

### 3. Shadowing (Überschreiben)

Innere Bindungen können äußere mit demselben Namen überschreiben:

```clojure
(let [x 1]
  (println "Äußeres x:" x)  ; => 1
  (let [x 2]
    (println "Inneres x:" x))  ; => 2
  (println "Zurück zum äußeren x:" x))  ; => 1
```

**Wichtig**: Die äußere Bindung bleibt unverändert - nur die Sichtbarkeit wird überschrieben.

### 4. Sequentielle Auswertung

Bindungen werden **sequentiell** ausgewertet, ähnlich wie `let*` in Common Lisp:

```clojure
(let [a 5
      b (* a 2)      ; kann auf 'a' zugreifen
      c (+ a b)]      ; kann auf 'a' und 'b' zugreifen
  c)  ; => 15
```

Spätere Bindungen können auf frühere verweisen, da sie in derselben Umgebung ausgewertet werden.

### 5. Implementierungsdetails

#### Clojure/ClojureScript Ansatz:

1. **Umgebung als Map**: Jede `let`-Umgebung ist eine Map (Hash-Map oder Array-Map)
2. **Umgebungskette**: Verschachtelte Umgebungen werden durch Kopieren der äußeren Umgebung erstellt
3. **Lokale Bindungen haben Vorrang**: Bei der Symbolauflösung wird zuerst in der lokalen Umgebung gesucht

#### Typische Implementierung:

```clojure
;; Pseudocode für let-Auflösung
(defn resolve-symbol [sym env]
  (or (get env sym)                    ; 1. Lokale let-Bindungen
      (resolve-in-closure sym)         ; 2. Closure-Umgebung
      (resolve-in-namespace sym)))     ; 3. Namespace
```

## Vergleich mit aktueller tiny-clj Implementierung

### Aktuelle Implementierung in `eval_let`:

```3037:3236:src/function_call.c
ID eval_let(CljList *list, CljMap *env, EvalState *st) {
    // (let [bindings*] body*)
    // bindings* => binding-form init-expr
    
    // Assertion: Environment must not be NULL when expected
    CLJ_ASSERT(env != NULL);
    
    if (!list || !st) {
        return NULL;
    }
    
    // Get bindings vector (second element): (let [x 10 y 20] ...)
    CljObject *bindings_vec = list_get_element(list, 1);
    if (!bindings_vec || !is_type(bindings_vec, CLJ_VECTOR)) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, 
                       "let requires a vector for bindings", 
                       NULL, 0, 0);
        return NULL;
    }
    
    CljPersistentVector *bindings = as_vector((CljValue)bindings_vec);
    if (!bindings) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, 
                       "let bindings must be a valid vector", 
                       NULL, 0, 0);
        return NULL;
    }
    int binding_count = bindings->count;
    
    // Bindings must come in pairs (symbol value symbol value ...)
    if (binding_count % 2 != 0) {
        throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, 
                       "let requires an even number of forms in binding vector", 
                       NULL, 0, 0);
        return NULL;
    }
    
    // Create new environment extending the current one
    // CRITICAL: Also include namespace mappings so functions like reverse are available
    // If no env provided, create new one with namespace mappings
    CljMap *let_env = NULL;
    int namespace_mapping_count = 0;
    if (st && st->current_ns && st->current_ns->mappings) {
        namespace_mapping_count = ((CljMap*)st->current_ns->mappings)->count;
    }
    
    if (!env) {
        // No parent environment - create new one with namespace mappings
        let_env = (CljMap*)make_map(binding_count / 2 + namespace_mapping_count + 4);
    } else {
        // Extend existing environment with namespace mappings
        let_env = (CljMap*)make_map(binding_count / 2 + env->count + namespace_mapping_count);
        if (let_env && env->count > 0) {
            // Copy existing environment bindings
            // CRITICAL: This includes function parameters from closure_env
            // When let is used inside a function, env is closure_env which contains function parameters
            for (int i = 0; i < env->capacity; i++) {
                CljValue key = env->data[i * 2];
                CljValue val = env->data[i * 2 + 1];
                if (key) {
                    // ASSERTION: Debug if we're copying 'coll' from env to let_env
                    if (is_type((CljObject*)key, CLJ_SYMBOL)) {
                        CljSymbol *key_sym = as_symbol((ID)key);
                        if (key_sym && key_sym->name && strcmp(key_sym->name, "coll") == 0) {
                            printf("[DEBUG] eval_let: Copying 'coll' from env to let_env, type: %d\n", 
                                   val && !IS_IMMEDIATE(val) ? ((CljObject*)val)->type : -1);
                        }
                    }
                    // CRITICAL: map_assoc may return a new map (COW or capacity growth), so we must update let_env
                    CljMap *new_let_env = (CljMap*)map_assoc((ID)let_env, (ID)key, (ID)val);
                    ASSIGN(let_env, new_let_env);
                }
            }
        }
    }
    
    // CRITICAL: Add clojure.core namespace mappings to let_env so functions like reverse are available
    // This ensures that when fn is evaluated inside let, the closure environment has namespace mappings
    // Use clojure.core cache instead of current_ns->mappings to ensure clojure.core functions are available
    extern TinyClJRuntime g_runtime;
    if (let_env && g_runtime.clojure_core_cache) {
        CljNamespace *clojure_core = (CljNamespace*)g_runtime.clojure_core_cache;
        if (clojure_core && clojure_core->mappings) {
            CljMap *ns_mappings = (CljMap*)clojure_core->mappings;
            for (int i = 0; i < ns_mappings->capacity; i++) {
                CljValue key = ns_mappings->data[i * 2];
                CljValue val = ns_mappings->data[i * 2 + 1];
                if (key) {
                    // Only add if not already in let_env (local bindings take precedence)
                    if (!map_contains((CljValue)let_env, (CljValue)key)) {
                        // CRITICAL: map_assoc may return a new map (COW or capacity growth), so we must update let_env
                        CljMap *new_let_env = (CljMap*)map_assoc((ID)let_env, (ID)key, (ID)val);
                        ASSIGN(let_env, new_let_env);
                    }
                }
            }
        }
    }
    
    if (!let_env) {
        return NULL;
    }
    
    // Process bindings sequentially (each binding can reference previous ones)
    for (int i = 0; i < binding_count; i += 2) {
        CljValue sym_val = bindings->data[i];
        CljValue init_val = bindings->data[i + 1];
        
        if (!sym_val || !is_type(sym_val, CLJ_SYMBOL)) {
            RELEASE(let_env);
            throw_exception(EXCEPTION_ILLEGAL_ARGUMENT, 
                           "let binding must be a symbol", 
                           NULL, 0, 0);
            return NULL;
        }
        
        // Evaluate init expression in the current let environment
        // This allows later bindings to reference earlier ones
        // Note: init_val can be NULL (nil), which is a valid value
        CljObject *value = NULL;
        
        if (!init_val) {
            // nil is represented as NULL - this is valid
            value = NULL;
        } else {
            // Check if init_val is an immediate value (doesn't need evaluation)
            if (is_fixnum(init_val) || is_special(init_val)) {
                // Immediate value - use as is
                value = init_val;
                RETAIN(value);  // Retain for consistency
            } else {
                // Complex expression - evaluate it
                value = eval_body(init_val, let_env, st);
                // value can be NULL if evaluation result is nil
            }
        }
        
        // CRITICAL: If value is a function (closure), update its closure_env to include itself
        // This allows recursive functions defined in let to find themselves
        // For example: (let [step (fn [x] (step x))] ...) - step needs to find itself
        if (value && is_type(value, CLJ_CLOSURE)) {
            CljFunction *func = as_function((ID)value);
            if (func && func->closure_env && is_type((CljObject*)func->closure_env, CLJ_MAP)) {
                // Add the function to its own closure environment so it can find itself recursively
                CljMap *func_env = (CljMap*)func->closure_env;
                // CRITICAL: map_assoc may return a new map (COW or capacity growth), so we must update closure_env
                // Always update closure_env to ensure the function can find itself recursively
                // Check if function is already in closure_env before adding
                CljObject *existing = (CljObject*)map_get((CljValue)func_env, (CljValue)sym_val);
                if (existing != value) {
                    // Function not in closure_env - add it
                    CljMap *new_func_env = (CljMap*)map_assoc((ID)func_env, (ID)sym_val, (ID)value);
                    // Update closure_env (map_assoc handles COW and capacity growth)
                    if (new_func_env != func_env) {
                        // Update closure_env if it changed (COW or capacity growth)
                        RELEASE((CljObject*)func->closure_env);
                        func->closure_env = (ID)new_func_env;
                        RETAIN((CljObject*)new_func_env);
                    } else {
                        // map_assoc returned same map but should have added the function
                        // Verify it's there now
                        CljObject *verify = (CljObject*)map_get((CljValue)func_env, (CljValue)sym_val);
                        if (verify != value) {
                            // Still not there - force update
                            // This shouldn't happen, but handle it
                            CljMap *forced_new = (CljMap*)map_assoc((ID)func_env, (ID)sym_val, (ID)value);
                            if (forced_new != func_env) {
                                RELEASE((CljObject*)func->closure_env);
                                func->closure_env = (ID)forced_new;
                                RETAIN((CljObject*)forced_new);
                            }
                        }
                    }
                }
            }
        }
        
        // Add binding to environment
        // CRITICAL: map_assoc may return a new map (COW or capacity growth), so we must update let_env
        CljMap *new_let_env = (CljMap*)map_assoc((ID)let_env, (ID)sym_val, (ID)value);
        // ASSIGN handles retain/release automatically and optimizes self-assignment
        ASSIGN(let_env, new_let_env);
        
        // Note: value is retained by map_assoc via RETAIN in map implementation
        // So we need to release our reference
        RELEASE(value);
    }
    
    // Evaluate body expressions with the let environment
    // Body is everything after the bindings vector
    CljObject *result = NULL;
    int list_len = list_count(list);
    
    if (list_len <= 2) {
        // No body expressions - return nil
        result = NULL;
    } else {
        // Evaluate all body expressions, return last one
        for (int i = 2; i < list_len; i++) {
            CljObject *body_expr = list_get_element(list, i);
```

### Analyse der aktuellen Implementierung:

**✅ Korrekt implementiert:**

1. **Umgebungskette**: Die äußere Umgebung (`env`) wird kopiert in `let_env`
2. **Sequentielle Auswertung**: Bindungen werden nacheinander verarbeitet, spätere können auf frühere verweisen
3. **Shadowing**: Lokale Bindungen überschreiben äußere (durch `map_assoc` nach dem Kopieren)
4. **Namespace-Mappings**: `clojure.core`-Funktionen werden zur Umgebung hinzugefügt

**⚠️ Potenzielle Verbesserungen:**

1. **Umgebungskette vs. flache Kopie**: Aktuell wird die gesamte Umgebung kopiert. In Clojure wird oft eine Umgebungskette verwendet (jede Umgebung verweist auf die nächste), was bei tiefen Verschachtelungen effizienter ist.

2. **Symbolauflösung**: Die aktuelle Implementierung sucht direkt in der Map. Eine explizite Umgebungskette würde die Auflösungslogik klarer machen.

## Empfehlungen

### 1. Umgebungskette (Environment Chain)

Statt die gesamte Umgebung zu kopieren, könnte eine Umgebungskette verwendet werden:

```c
typedef struct {
    CljMap *local_bindings;    // Nur lokale Bindungen
    CljMap *parent_env;         // Verweis auf äußere Umgebung
} EnvironmentChain;
```

**Vorteile:**
- Weniger Speicher bei tiefen Verschachtelungen
- Klarere Trennung zwischen lokalen und äußeren Bindungen
- Einfacheres Debugging

**Nachteile:**
- Komplexere Auflösungslogik (muss Kette durchlaufen)
- Mehr Pointer-Dereferenzierungen

### 2. Aktuelle Implementierung beibehalten

Die aktuelle Implementierung ist **korrekt** und folgt dem Clojure-Modell. Die flache Kopie ist für die meisten Anwendungsfälle ausreichend und einfacher zu verstehen.

## Testfälle

Die aktuelle Implementierung deckt bereits die wichtigsten Szenarien ab:

```98:130:src/tests/test_let.c
// ============================================================================
// TEST: Nested let
// ============================================================================
TEST(test_let_nested) {
    WITH_AUTORELEASE_POOL({
        // Use global st from setUp (clojure.core already loaded)
        
        // Test: (let [x 10] (let [y 20] (+ x y))) should return 30
        const char *code = "(let [x 10] (let [y 20] (+ x y)))";
        CljValue result = eval_string(code, g_test_eval_state);
        
        TEST_ASSERT_NOT_NULL(result);
        TEST_ASSERT_TRUE(is_fixnum(result));
        TEST_ASSERT_EQUAL_INT(30, as_fixnum(result));
    });
}

// ============================================================================
// TEST: Let shadowing outer binding
// ============================================================================
TEST(test_let_shadowing) {
    WITH_AUTORELEASE_POOL({
        // Use global st from setUp (clojure.core already loaded)
        
        // Test: (let [x 10] (let [x 20] x)) should return 20 (inner shadows outer)
        const char *code = "(let [x 10] (let [x 20] x))";
        CljValue result = eval_string(code, g_test_eval_state);
        
        TEST_ASSERT_NOT_NULL(result);
        TEST_ASSERT_TRUE(is_fixnum(result));
        TEST_ASSERT_EQUAL_INT(20, as_fixnum(result));
    });
}
```

## Zusammenfassung

Die aktuelle `tiny-clj` Implementierung von `let`-Bindungen entspricht dem Clojure-Modell:

- ✅ Lexikalisches Scoping
- ✅ Verschachtelte Umgebungen durch Kopieren
- ✅ Sequentielle Auswertung
- ✅ Shadowing von äußeren Bindungen
- ✅ Zugriff auf Namespace-Funktionen

Die Implementierung ist **korrekt** und folgt den Best Practices von Clojure/ClojureScript. Eine Umgebungskette wäre eine mögliche Optimierung für tief verschachtelte Kontexte, ist aber für die meisten Anwendungsfälle nicht notwendig.

## Quellen

- Clojure Special Forms Dokumentation: https://clojure.org/reference/special_forms
- Clojure Destructuring Guide: https://clojure.org/guides/destructuring
- Stack Overflow Diskussionen zu let-Bindungen
- ClojureScript Compiler Implementierung (Community-Diskussionen)





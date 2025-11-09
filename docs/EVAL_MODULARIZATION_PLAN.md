# Plan: Evaluation Modularization

## Ziel

Refaktorierung der großen `eval_list_with_ctx` Funktion (>500 Zeilen) in kleinere, wartbare Module.

## Phase 1: Erweiterte Dispatch-Funktionen

### Aufgaben
- [ ] Erstelle `eval_special_forms_dispatch` für Special Forms (if, when, while, cond, quote, quasiquote, do)
- [ ] Erstelle `eval_binding_dispatch` für Bindings (let, loop, recur)
- [ ] Erstelle `eval_definition_dispatch` für Definitionen (def, defn, var, ns)
- [ ] Erstelle `eval_function_call_dispatch` für Funktionenaufrufe
- [ ] Refaktoriere `eval_list_with_ctx` um diese Dispatch-Funktionen zu verwenden

### Struktur
```c
ID eval_list_with_ctx(CljList *list, CljMap *env, EvalState *st, const EvalContext *ctx) {
    // 1. Operator auflösen
    CljObject *op = resolve_operator(list, env, st, ctx);
    
    // 2. Dispatch zu spezialisierten Funktionen (frequenzbasiert)
    CljObject *result = NULL;
    
    // Tier 1: Arithmetik (90%+)
    result = eval_arithmetic_dispatch(list, env, st, ctx, op);
    if (result) return result;
    
    // Tier 2: Vergleich (70-90%)
    result = eval_comparison_dispatch(list, env, st, ctx, op);
    if (result) return result;
    
    // Tier 3: Special Forms (50-70%)
    result = eval_special_forms_dispatch(list, env, st, ctx, op);
    if (result) return result;
    
    // Tier 4: Bindings (30-50%)
    result = eval_binding_dispatch(list, env, st, ctx, op);
    if (result) return result;
    
    // Tier 5: Definitionen (10-30%)
    result = eval_definition_dispatch(list, env, st, ctx, op);
    if (result) return result;
    
    // Tier 6: Sequenzen (10-30%)
    result = eval_sequence_dispatch(list, env, st, ctx, op);
    if (result) return result;
    
    // Tier 7: Loops (<10%)
    result = eval_loop_dispatch(list, env, st, ctx, op);
    if (result) return result;
    
    // Fallback: Funktionenaufruf
    return eval_function_call_dispatch(list, env, st, ctx, op);
}
```

## Phase 2: Spezialisierte Handler-Funktionen

### Aufgaben
- [ ] Implementiere `eval_if_handler` für `if`
- [ ] Implementiere `eval_when_handler` für `when`
- [ ] Implementiere `eval_while_handler` für `while`
- [ ] Implementiere `eval_cond_handler` für `cond`
- [ ] Implementiere `eval_quote_handler` für `quote`
- [ ] Implementiere `eval_quasiquote_handler` für `quasiquote`
- [ ] Implementiere `eval_do_handler` für `do`
- [ ] Implementiere `eval_let_handler` für `let`
- [ ] Implementiere `eval_loop_handler` für `loop`
- [ ] Implementiere `eval_recur_handler` für `recur`
- [ ] Implementiere `eval_def_handler` für `def`
- [ ] Implementiere `eval_defn_handler` für `defn`
- [ ] Implementiere `eval_var_handler` für `var`
- [ ] Implementiere `eval_ns_handler` für `ns`

### Beispiel-Implementierung
```c
static CljObject* eval_special_forms_dispatch(CljList *list, CljMap *env, EvalState *st, const EvalContext *ctx, CljObject *op) {
    if (op == SYM_IF) return eval_if_handler(list, env, st, ctx);
    if (op == SYM_WHEN) return eval_when_handler(list, env, st, ctx);
    if (op == SYM_WHILE) return eval_while_handler(list, env, st, ctx);
    if (op == SYM_COND) return eval_cond_handler(list, env, st, ctx);
    if (op == SYM_QUOTE) return eval_quote_handler(list, env, st, ctx);
    if (op == SYM_QUASIQUOTE) return eval_quasiquote_handler(list, env, st, ctx);
    if (op == SYM_DO) return eval_do_handler(list, env, st, ctx);
    return NULL;
}

static CljObject* eval_if_handler(CljList *list, CljMap *env, EvalState *st, const EvalContext *ctx) {
    // (if cond then else?)
    CljObject *cond_val = eval_arg_with_ctx(list, 1, env, st, ctx);
    bool truthy = clj_is_truthy(cond_val);
    if (cond_val) RELEASE(cond_val);
    
    CljObject *branch = truthy ? list_get_element(list, 2) : list_get_element(list, 3);
    if (!branch) return NULL;
    
    return eval_body_with_ctx(branch, env, st, ctx);
}
```

## Phase 3: Gemeinsame Hilfsfunktionen

### Aufgaben
- [ ] Implementiere `eval_arg_with_ctx` für Argument-Evaluierung
- [ ] Implementiere `eval_body_with_ctx` für Body-Evaluierung
- [ ] Implementiere `resolve_operator` für Operator-Auflösung
- [ ] Implementiere `resolve_symbol_with_ctx` für Symbol-Auflösung
- [ ] Extrahiere gemeinsame Patterns aus bestehenden Handler-Funktionen

### Beispiel-Implementierung
```c
// Gemeinsame Argument-Evaluierung
static ID eval_arg_with_ctx(CljList *list, int index, CljMap *env, EvalState *st, const EvalContext *ctx) {
    CljObject *arg_expr = list_get_element(list, index);
    if (!arg_expr) return NULL;
    return eval_body_with_ctx(arg_expr, env, st, ctx);
}

// Gemeinsame Body-Evaluierung
static ID eval_body_with_ctx(ID body, CljMap *env, EvalState *st, const EvalContext *ctx) {
    if (IS_IMMEDIATE(body)) return body;
    
    if (is_type((CljObject*)body, CLJ_LIST)) {
        return eval_list_with_ctx(as_list((ID)body), env, st, ctx);
    }
    
    if (is_type((CljObject*)body, CLJ_SYMBOL)) {
        return resolve_symbol_with_ctx((CljSymbol*)body, env, st, ctx);
    }
    
    return RETAIN(body);
}

// Gemeinsame Operator-Auflösung
static CljObject* resolve_operator(CljList *list, CljMap *env, EvalState *st, const EvalContext *ctx) {
    CljObject *op = list->first;
    
    // Nested list evaluation
    if (is_type(op, CLJ_LIST)) {
        return eval_list_with_ctx(as_list((ID)op), env, st, ctx);
    }
    
    // Symbol resolution
    if (is_type(op, CLJ_SYMBOL)) {
        // 1. Check closure_env (for recursive calls)
        if (ctx && ctx->env && ctx->env->closure_env) {
            CljObject *resolved = map_get(ctx->env->closure_env, op);
            if (resolved) return resolved;
        }
        
        // 2. Check local environment
        if (env) {
            CljObject *resolved = map_get(env, op);
            if (resolved) return resolved;
        }
        
        // 3. Check namespace
        if (st) {
            CljObject *resolved = ns_resolve(st, op);
            if (resolved) return resolved;
        }
    }
    
    return op;
}
```

## Migrationsstrategie

### Schritt 1: Vorbereitung
- [ ] Erstelle neue Dispatch-Funktionen parallel zur bestehenden Implementierung
- [ ] Stelle sicher, dass alle Tests bestehen

### Schritt 2: Migration
- [ ] Verschiebe `if` in `eval_if_handler`
- [ ] Teste `if` isoliert
- [ ] Verschiebe `when` in `eval_when_handler`
- [ ] Teste `when` isoliert
- [ ] Verschiebe `while` in `eval_while_handler`
- [ ] Teste `while` isoliert
- [ ] Verschiebe `cond` in `eval_cond_handler`
- [ ] Teste `cond` isoliert
- [ ] Verschiebe `let` in `eval_let_handler`
- [ ] Teste `let` isoliert
- [ ] Verschiebe `def` in `eval_def_handler`
- [ ] Teste `def` isoliert
- [ ] Verschiebe `defn` in `eval_defn_handler`
- [ ] Teste `defn` isoliert
- [ ] Verschiebe weitere Operatoren nach Bedarf

### Schritt 3: Aufräumen
- [ ] Entferne alte Implementierung aus `eval_list_with_ctx`
- [ ] Stelle sicher, dass alle Tests bestehen
- [ ] Führe Performance-Tests durch
- [ ] Dokumentiere die neue Struktur

## Erfolgskriterien

- [ ] `eval_list_with_ctx` hat weniger als 100 Zeilen
- [ ] Jede Handler-Funktion hat weniger als 50 Zeilen
- [ ] Alle Tests bestehen
- [ ] Performance bleibt gleich oder verbessert sich
- [ ] Code ist wartbarer und testbarer

## Referenzen

- Common Lisp: Dispatch-Tabellen für Operatoren
- Clojure-Compiler: Spezialisierte Dispatch-Funktionen
- Scheme: Visitor-Pattern für AST-Walker
- Strategie-Pattern: Funktionszeiger für Operatoren


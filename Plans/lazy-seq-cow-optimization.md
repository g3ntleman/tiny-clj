# Plan: CljLazySeq COW-Optimierung (Macro-basiert)

Datum: 2026-01-22

## Zusammenfassung

`for` wird als Macro implementiert, das auf `lazy-seq` aufsetzt. Der Iterations-State liegt in einer **Map im `env_stack`** der Closure. Bei `rc==1` wird diese Map via `map_assoc_inplace` in-place mutiert - **Zero-Allocation pro Iteration**.

## Design-Prinzipien

1. **Keine neuen Typen** - Kein CLJ_THUNK_STATE, nur bestehende Strukturen
2. **Thunk = normale CljFunction** - Mit State-Map in `env_stack[0]`
3. **COW via bestehende Infrastruktur** - `map_assoc_inplace`, `vector_assoc_inplace`
4. **Zero-Allocation bei rc==1** - Map wird in-place mutiert, LazySeq wiederverwendet

## Datenstrukturen (UNVERÄNDERT)

```c
// CljLazySeq - KEINE Änderung nötig
typedef struct {
    CljObject base;     // type=CLJ_LAZY_SEQ, rc
    ID first;           // NOT_FOUND=unrealized, NULL=nil, sonst Element
    ID thunk;           // 0-arity CljFunction (normale Closure!)
    ID cached_rest;     // NOT_FOUND=unrealized, NULL=empty, sonst rest-seq
} CljLazySeq;

// CljFunction - KEINE Änderung nötig
typedef struct {
    CljObject base;
    CljVector *params;      // []  (0-arity für Thunks)
    ID body;                // Body-AST
    CljVector *env_stack;   // [state-map, ...outer-envs...]
    const char *name;
    struct CljNamespace *ns;
    int8_t variadic_index;
} CljFunction;
```

## State-Map Struktur

Der Thunk captured eine State-Map als `env_stack[0]`:

```clojure
{:__seq__  <current-seq>   ; Die aktuelle Position im Input
 :__var__  <binding-sym>   ; Das Binding-Symbol (für eval)
 :__body__ <body-ast>}     ; Der Body (für eval)
```

## Implementierung

### 1. for-Macro in clojure.core.clj

```clojure
(defmacro for [[binding coll] body]
  `(let [s# (seq ~coll)]
     ((fn for-step# [xs#]
        (lazy-seq
          (when xs#
            (let [~binding (first xs#)]
              (cons ~body (for-step# (next xs#)))))))
      s#)))
```

### 2. COW-Logik in lazy_seq_realize (seq.c)

```c
static void lazy_seq_realize(CljLazySeq *lazy) {
    if (!lazy || lazy->first != NOT_FOUND) return;
    if (!lazy->thunk) {
        lazy->first = NULL;
        lazy->cached_rest = NULL;
        return;
    }

    // === COW DECISION ===
    // Wenn lazy.rc == 1 UND thunk.rc == 1: können wir in-place mutieren
    CljFunction *thunk = as_function(lazy->thunk);
    bool can_mutate = (lazy->base.rc == 1) && 
                      thunk && (thunk->base.rc == 1);
    
    if (!can_mutate && lazy->base.rc > 1) {
        // Multi-consumer: Clone die Closure mit eigenem env_stack
        CljFunction *cloned = clone_closure_with_env(thunk);
        RELEASE(lazy->thunk);
        lazy->thunk = (ID)cloned;
        thunk = cloned;
    }

    // Standard: Thunk evaluieren
    EvalState *st = builtin_get_eval_state();
    if (!st) st = get_global_eval_state();
    
    builtin_set_eval_state(st);
    ID seq_val = eval_function_call(lazy->thunk, NULL, 0, NULL, st);
    builtin_set_eval_state(NULL);

    // Rest wie bisher: first/rest cachen...
    ID first_val = NULL;
    ID rest_val = NULL;
    if (seq_val) {
        ID seq_obj = native_seq(&seq_val, 1);
        if (seq_obj) {
            first_val = native_first(&seq_obj, 1);
            rest_val = native_rest(&seq_obj, 1);
        }
    }
    
    ASSIGN(lazy->first, first_val ? first_val : SYM_NIL);
    ASSIGN(lazy->cached_rest, rest_val);
    RELEASE(lazy->thunk);
    lazy->thunk = NULL;
}
```

### 3. clone_closure_with_env (neu in function.c)

```c
CljFunction* clone_closure_with_env(CljFunction *src) {
    if (!src) return NULL;
    
    // Shallow copy der Closure
    CljFunction *dst = malloc(sizeof(CljFunction));
    dst->base.type = CLJ_CLOSURE;
    dst->base.rc = 1;
    dst->base.flags = 0;
    
    dst->params = src->params ? (CljVector*)RETAIN(src->params) : NULL;
    dst->body = RETAIN(src->body);
    dst->name = src->name ? strdup(src->name) : NULL;
    dst->ns = src->ns ? (struct CljNamespace*)RETAIN(src->ns) : NULL;
    dst->variadic_index = src->variadic_index;
    
    // env_stack: Shallow clone (Maps werden geteilt, aber Vector ist neu)
    if (src->env_stack) {
        unsigned int count = vector_count(src->env_stack);
        dst->env_stack = make_vector(count, CLJ_VECTOR_PERSISTENT);
        for (unsigned int i = 0; i < count; i++) {
            ID env_map = vector_nth(src->env_stack, i);
            // Maps werden nur RETAIN'd, nicht deep-copied
            // COW passiert automatisch bei map_assoc_inplace
            vector_conj_inplace(&dst->env_stack, RETAIN(env_map));
        }
    } else {
        dst->env_stack = NULL;
    }
    
    return dst;
}
```

## Memory-Verhalten

### Single-Consumer (rc==1)

```
(doall (for [x (range 1000)] x))

Iteration 1: 
  lazy.rc=1, thunk.rc=1
  → Thunk wird evaluiert
  → env_stack[0] Map wird in-place mutiert (map_assoc_inplace)
  → 0 neue Allokationen

Iteration 2-1000:
  → Gleich, jeweils 0 neue Allokationen
  
Total: ~3 mallocs (LazySeq + Thunk + initial State-Map)
```

### Multi-Consumer (rc>1)

```
(def s (for [x (range 10)] x))
(def a s)   ; s.rc = 2
(def b s)   ; s.rc = 3

(first a)   ; → clone_closure_with_env → 1 malloc
(first b)   ; → clone_closure_with_env → 1 malloc

;; a und b iterieren unabhängig
```

## Wichtige Dateien

| Datei | Änderung |
|-------|----------|
| `src/clojure.core.clj` | for-Macro Definition |
| `src/seq.c` | COW-Logik in lazy_seq_realize |
| `src/function.c` | clone_closure_with_env() hinzufügen |
| `src/function.h` | Deklaration clone_closure_with_env() |

## Implementierungsschritte

1. **for-Macro** in clojure.core.clj implementieren (ersetzt eval_for)
2. **clone_closure_with_env()** in function.c hinzufügen
3. **lazy_seq_realize** erweitern mit COW-Check
4. **Tests** für COW-Verhalten
5. **Benchmarks** für Allocation-Count

## Tests

```clojure
;; Single consumer - sollte zero-alloc sein nach Setup
(let [s (for [x (range 100)] x)]
  (doall s))

;; Multi consumer - COW
(let [s (for [x (range 10)] x)
      a s
      b s]
  (and (= (doall a) '(0 1 2 3 4 5 6 7 8 9))
       (= (doall b) '(0 1 2 3 4 5 6 7 8 9))))

;; Nested for
(doall (for [x (range 3)]
         (doall (for [y (range 3)]
                  [x y]))))
```

## Risiken

1. **Self-Reference**: Bei rc==1 könnte `cached_rest` auf `self` zeigen - aber da wir `thunk` releasen und `cached_rest` setzen, ist das kein Problem
2. **Thread-Safety**: Nicht nötig für embedded single-threaded
3. **Nested for**: Jede Closure hat eigenen env_stack, funktioniert automatisch

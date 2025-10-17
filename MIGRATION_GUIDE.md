# Tiny-Clj Migration Guide: CljObject* → CljValue

## Übersicht

Dieser Guide hilft bei der Migration von der alten `CljObject*` API zur neuen `CljValue` API in Tiny-Clj. Die neue API bietet bessere Performance durch 32-bit Tagged Pointers für Immediate Values und eine konsistentere API.

## Was ist CljValue?

`CljValue` ist ein typedef für `CljObject*`, aber mit erweiterten Fähigkeiten:

- **Immediate Values**: Kleine Zahlen, Zeichen, Booleans und `nil` werden als 32-bit Tagged Pointers gespeichert (keine Heap-Allokation)
- **Heap Objects**: Komplexe Datenstrukturen (Vektoren, Maps, Strings) werden weiterhin auf dem Heap allokiert
- **Transparente API**: Die meisten Funktionen funktionieren mit beiden Typen

## Migration Patterns

### 1. Objekt-Erstellung

**Alt:**
```c
CljObject *vec = make_vector(10, 1);
CljObject *map = make_map(16);
CljObject *str = make_string("hello");
```

**Neu:**
```c
CljValue vec = make_vector_v(10, 1);
CljValue map = make_map_v(16);
CljValue str = make_string_v("hello");
```

### 2. Type Checking

**Alt:**
```c
if (obj->type == CLJ_VECTOR) {
    CljPersistentVector *vec_data = as_vector(obj);
    // ...
}
```

**Neu:**
```c
if (is_immediate_value(vec)) {
    // Handle immediate value
    if (is_fixnum(vec)) {
        int val = as_fixnum(vec);
        // ...
    }
} else {
    // Handle heap object
    if (((CljObject*)vec)->type == CLJ_VECTOR) {
        CljPersistentVector *vec_data = as_vector((CljObject*)vec);
        // ...
    }
}
```

### 3. Memory Management

**Alt:**
```c
CljObject *vec = make_vector(10, 1);
// ... use vec ...
RELEASE(vec);
```

**Neu:**
```c
CljValue vec = make_vector_v(10, 1);
// ... use vec ...
// Immediate values: Kein RELEASE nötig
if (!is_immediate_value(vec)) {
    RELEASE((CljObject*)vec);
}
```

### 4. Vector Operations

**Alt:**
```c
CljObject *vec = make_vector(5, 1);
CljObject *new_vec = vector_conj(vec, item);
RELEASE(vec);
RELEASE(new_vec);
```

**Neu:**
```c
CljValue vec = make_vector_v(5, 1);
CljValue new_vec = vector_conj_v(vec, item);
// Memory management wie oben
```

### 5. Map Operations

**Alt:**
```c
CljMap *map = make_map(16);
map_assoc(map, key, value);
CljObject *result = map_get(map, key);
```

**Neu:**
```c
CljValue map = make_map_v(16);
map_assoc_v(map, key, value);
CljValue result = map_get_v(map, key);
```

### 6. Transient Collections

**Neu (nur mit CljValue API):**
```c
// Persistent → Transient
CljValue vec = make_vector_v(5, 1);
CljValue tvec = transient(vec);

// Transient Operations
CljValue new_tvec = conj_v(tvec, item);

// Transient → Persistent
CljValue persistent_vec = persistent_v(new_tvec);
```

## Immediate Values

### Erkennung
```c
if (is_immediate_value(val)) {
    if (is_fixnum(val)) {
        int n = as_fixnum(val);
    } else if (is_char(val)) {
        char c = as_char(val);
    } else if (is_special(val)) {
        if (is_nil(val)) {
            // Handle nil
        } else if (is_true(val)) {
            // Handle true
        } else if (is_false(val)) {
            // Handle false
        }
    }
}
```

### Erstellung
```c
CljValue num = make_fixnum(42);        // Immediate
CljValue ch = make_char('A');         // Immediate
CljValue nil_val = make_nil();        // Immediate
CljValue bool_val = make_bool(true);  // Immediate
```

## Best Practices

### 1. Immer auf Immediate Values prüfen
```c
void process_value(CljValue val) {
    if (is_immediate_value(val)) {
        // Handle immediate - no memory management needed
        return;
    }
    
    // Handle heap object
    CljObject *obj = (CljObject*)val;
    // ... use obj ...
    RELEASE(obj);
}
```

### 2. Casting für Legacy APIs
```c
CljValue vec = make_vector_v(10, 1);
// Wenn Legacy API CljObject* erwartet:
legacy_function((CljObject*)vec);
```

### 3. Memory Management Pattern
```c
CljValue result = some_function();
if (!is_immediate_value(result)) {
    // Nur Heap-Objekte müssen released werden
    RELEASE((CljObject*)result);
}
```

## Breaking Changes

### Entfernte APIs
Die folgenden APIs sind als deprecated markiert und werden in zukünftigen Versionen entfernt:

- `make_vector()` → `make_vector_v()`
- `vector_conj()` → `vector_conj_v()`
- `make_map()` → `make_map_v()`
- `map_get()` → `map_get_v()`
- `map_assoc()` → `map_assoc_v()`
- `make_string()` → `make_string_v()`
- `parse()` → `parse_v()`

### Neue APIs
- `transient()` - Konvertiert persistent zu transient
- `conj_v()` - Transient vector operations
- `persistent_v()` - Konvertiert transient zu persistent
- `transient_map()` - Transient map operations
- `conj_map_v()` - Transient map operations
- `persistent_map_v()` - Transient map operations

## Performance Vorteile

### Memory Efficiency
- **Immediate Values**: Keine Heap-Allokation für kleine Zahlen, Zeichen, Booleans
- **Reduced GC Pressure**: Weniger Objekte auf dem Heap
- **Cache Friendly**: Bessere CPU-Cache-Nutzung

### Beispiel
```c
// Alt: 2 Heap-Allokationen
CljObject *a = make_int(42);    // Heap allocation
CljObject *b = make_int(43);    // Heap allocation

// Neu: 0 Heap-Allokationen
CljValue a = make_fixnum(42);   // Tagged pointer
CljValue b = make_fixnum(43);   // Tagged pointer
```

## Migration Checklist

- [ ] Ersetze `make_*()` Aufrufe durch `make_*_v()`
- [ ] Ersetze `*_conj()` Aufrufe durch `*_conj_v()`
- [ ] Ersetze `map_*()` Aufrufe durch `map_*_v()`
- [ ] Füge `is_immediate_value()` Checks hinzu
- [ ] Aktualisiere Memory Management
- [ ] Teste mit neuen APIs
- [ ] Entferne deprecated API Aufrufe

## Beispiele

### Komplettes Beispiel: Vector Processing
```c
// Alt
void process_vector_old(CljObject *items[], int count) {
    CljObject *vec = make_vector(count, 1);
    CljPersistentVector *vec_data = as_vector(vec);
    
    for (int i = 0; i < count; i++) {
        vec_data->data[i] = RETAIN(items[i]);
    }
    vec_data->count = count;
    
    // Process vector...
    
    RELEASE(vec);
}

// Neu
void process_vector_new(CljValue items[], int count) {
    CljValue vec = make_vector_v(count, 1);
    CljPersistentVector *vec_data = as_vector((CljObject*)vec);
    
    for (int i = 0; i < count; i++) {
        if (is_immediate_value(items[i])) {
            vec_data->data[i] = (CljObject*)items[i];
        } else {
            vec_data->data[i] = RETAIN((CljObject*)items[i]);
        }
    }
    vec_data->count = count;
    
    // Process vector...
    
    if (!is_immediate_value(vec)) {
        RELEASE((CljObject*)vec);
    }
}
```

## Hilfe

Bei Fragen zur Migration:
1. Prüfe die Header-Dateien für verfügbare APIs
2. Schaue in die Test-Dateien für Beispiele
3. Verwende `is_immediate_value()` für Type-Checking
4. Beachte Memory Management für Heap-Objekte

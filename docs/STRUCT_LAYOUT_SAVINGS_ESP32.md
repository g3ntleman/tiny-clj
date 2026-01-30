# Struct Layout – Einsparungspotential (32-bit ESP32 / ILP32)

Analyse der wichtigsten Structs in subjective-c und src für **ESP32 (32-bit)**. Annahme: **4-Byte-Pointer**, **4-Byte-Ausrichtung** (ILP32). Viele Structs haben auf 32-bit **kein Padding**, das auf 64-bit LP64 entsteht; verbleibende Lücken und Reorder-Optionen sind hier erfasst.

---

## 0. malloc / Heap-Padding auf ESP-IDF

ESP-IDF nutzt **TLSF** (components/heap/tlsf) über `heap_caps_malloc` bzw. `malloc`. Pro Allokation kommt folgendes dazu:

| Komponente | 32-bit (ESP32) | Anmerkung |
|------------|----------------|-----------|
| **TLSF Block-Header** | **8 Bytes** vor Nutzerdaten | `block_start_offset` = `offsetof(block_header_t, size) + sizeof(size_t)`; `block_header_t`: prev_phys_block(4) + size(4), Nutzerptr beginnt danach |
| **Ausrichtung** | **4 Bytes** (ALIGN_SIZE) | Angeforderte Größe wird auf Vielfaches von 4 aufgerundet |
| **Mindest-Nutzergröße** | 4 Bytes | Kleinere Anfragen → 4 Bytes nutzbar, Block insgesamt 8+4 = **12 Bytes** |
| **Poisoning** (CONFIG_HEAP_POISONING) | **+12 Bytes** pro Block | `poison_head_t` (4+4) vor + `poison_tail_t` (4) nach Nutzerdaten → **+20 Bytes Overhead gesamt** (8 TLSF + 12 Poison) |

**Folgen für Tiny-CLJ auf ESP32:**

- **Pro malloc(n):** tatsächlich belegt ≈ 8 + align4(n) Bytes (ohne Poison) bzw. 20 + align4(n) mit Poison.
- Sehr kleine Structs (z. B. **CljObject 4 B**) kosten **12 Bytes** (8 Header + 4) → Faktor 3. Weniger, dafür größere Allokationen (Pools, Block-Allokatoren) reduzieren Overhead.
- Struct-Größen auf **Vielfache von 4** bringen: Kein Aufrunden durch den Allocator; Lücken im Struct möglichst nutzen (Reorder), damit keine unnötige Größe entsteht.
- **Empfehlung:** Bei RAM-Knappheit CONFIG_HEAP_POISONING nur für Debug; Struct-Layout so wählen, dass Größe ≡ 0 (mod 4) und möglichst wenig „Verschnitt“ (siehe Abschnitte unten).

---

## 1. CljObject (object.h) – bereits optimal

```c
uint8_t type;
uint8_t flags;
int16_t rc;
```

- **Größe:** 4 Bytes, keine Lücken.
- **Änderung:** Keine.

---

## 2. CljPersistentVector (vector.c)

```c
CljObject base;      // 4
unsigned int count;  // 4
int capacity;        // 4
ID data[];           // flex, 4-Byte-ausgerichtet
```

- **ESP32:** 12 Bytes fest, `data[]` startet bei Offset 12 → **kein Padding** (auf 64-bit: 4 Bytes Verschnitt).
- **Option:** Keine Layout-Änderung nötig.

---

## 3. CljTransientVector (vector.h)

```c
CljObject base;              // 4
CljPersistentVector *backing; // 4
```

- **ESP32:** 4 + 4 = **8 Bytes** → kein Padding (auf 64-bit: 16 Bytes mit 4 Bytes Padding).
- **Änderung:** Keine.

---

## 4. CljSymbol (symbol.h)

```c
CljObject base;
struct CljSymbol *ns_name;
struct CljSymbol *unqualified;
const char *cname;
```

- **ESP32:** 4 + 4 + 4 + 4 = **16 Bytes**, 4-Byte-ausgerichtet → **kein Padding** (auf 64-bit: 32 Bytes, 4 Bytes am Ende).
- **Einsparung:** Optional ein kleines Nutzfeld (z. B. `uint16_t flags`) einplanen für spätere Erweiterung ohne Struct-Vergrößerung.

---

## 5. CljNamespace (namespace.h)

```c
CljObject base;
CljSymbol *name;
CljMap *mappings;
CljMap *macro_mappings;
CljMap *aliases;
bool loaded;
const char *filename;
```

- **ESP32:** base(4) + 4×4 + 1 = 21, dann **3 Bytes Padding** für 4-Byte-Ausrichtung von `filename`, dann 4 → **28 Bytes** (3 Bytes Padding).
- **Einsparung:** Kleine Felder direkt hinter `base`:  
  `base(4), loaded(1), padding(3), name(4), mappings(4), macro_mappings(4), aliases(4), filename(4)`  
  → weiterhin **28 Bytes**, Padding sinnvoll genutzt; bei Erweiterung um 1–3 Byte-Felder keine Vergrößerung.

---

## 6. CljList / CljASTNode (list.h, ast.h)

- **CljList:** base(4) + first(4) + rest(4) = **12 Bytes** → kein Padding (64-bit: 24 Bytes, 4 Bytes Padding).
- **CljASTNode:** base(4) + first(4) + rest(4) + callsite_cache(4) = **16 Bytes** → kein Padding (64-bit: 32 Bytes).
- **Änderung:** Keine nötig.

---

## 7. CljMap (map.h)

```c
CljObject base;
int count;
int capacity;
CljObject *data[];
```

- **ESP32:** 4 + 4 + 4 = 12, `data[]` bei Offset 12 → **kein Padding** (64-bit: 4 Bytes Padding).
- **Änderung:** Keine.

---

## 8. CljFunction (function.h)

```c
CljObject base;
CljPersistentVector *params;
ID body;
CljPersistentVector *env_stack;
struct CljSymbol *name_sym;
struct CljNamespace *ns;
int8_t variadic_index;
```

- **ESP32:** 4 + 5×4 + 1 = 25, + **3 Bytes Padding** → **28 Bytes**.
- **Einsparung:** `variadic_index` direkt nach `base`:  
  `base(4), variadic_index(1), padding(3), params(4), body(4), env_stack(4), name_sym(4), ns(4)`  
  → weiterhin 28 Bytes, Padding genutzt; spätere 1–3 Byte-Felder ohne Vergrößerung.

---

## 9. CljByteArray (byte_array.h)

```c
CljObject base;
int length;
uint8_t *data;
```

- **ESP32:** 4 + 4 + 4 = **12 Bytes** → kein Padding (64-bit: 24 Bytes, 4 Bytes Padding).
- **Änderung:** Keine.

---

## 10. CljSlotRef (ast.h)

```c
CljObject base;
CljSymbol *symbol;
uint8_t depth;
uint8_t slot;
```

- **ESP32:** 4 + 4 + 1 + 1 = 10, + **2 Bytes Padding** → **12 Bytes** (64-bit: 24 Bytes, 10 Bytes Verschnitt).
- **Einsparung:** Kleine Felder vor Pointer:  
  `base(4), depth(1), slot(1), padding(2), symbol(4)`  
  → **12 Bytes**, gleiche Größe; weniger „Loch“ in der Mitte, bessere Auslastung.

---

## 11. CLJException (exception.h)

```c
CljObject base;
char type[64];
char message[256];
char file[128];
int line;
int col;
#ifdef DEBUG
  struct CljString *stacktrace;
  uintptr_t object;
#endif
```

- **ESP32:** base(4) + type(64) + message(256) + file(128) + line(4) + col(4) = **460 Bytes** (+ DEBUG: 4 + 4 = 8 → **468 Bytes**). Kein Padding nötig (Arrays 4-Byte-ausgerichtet). Auf 64-bit: 464+ durch Pointer-Padding.
- **Einsparung:** `line`/`col` direkt nach `base` spart kein Padding auf 32-bit; Gesamtgröße durch Arrays dominiert. **Größerer Effekt:** Kürzere Puffer (z. B. type[32], message[128], file[64]) spart RAM, ist semantische Änderung.

---

## 12. CljHashMap (hashmap.h)

```c
CljObject base;
unsigned int count;
unsigned int capacity;
size_t tombstones;  // 4 auf ESP32
CljObject *data[];
```

- **ESP32:** 4 + 4 + 4 + 4 = 16, `data[]` bei 16 → **kein Padding**.
- **Änderung:** Keine.

---

## 13. TinyClJRuntime (runtime.h)

- Auf 32-bit: viele Pointer je 4 Byte; kleine Felder gruppieren reduziert ggf. minimales Padding. Nur eine Instanz; Einsparung im Byte-Bereich.

---

## 14. EvalState (namespace.h)

- Pointer und `int` je 4 Byte; Layout bereits gut. Kleine Felder zusammenrücken optional.

---

## 15. CallFrame (environment.h)

```c
struct CallFrame *parent;
ID *params;
int param_count;
ID values[CALLFRAME_MAX_PARAMS];  // 16
```

- **ESP32:** 4 + 4 + 4 + 16×4 = **76 Bytes** → kein Padding (64-bit: 152 Bytes).
- **Änderung:** Optional `param_count` direkt hinter `parent`; Größe gleich.

---

## 16. LineEditorState (line_editor.h)

```c
char buffer[512];
int cursor_pos;
int length;
bool line_ready;
```

- **ESP32:** 512 + 4 + 4 + 1 = 521, + **3 Padding** → **524 Bytes**.
- **Einsparung:** Wie auf 64-bit: kleine Felder vor Puffer oder `cursor_pos`/`length` als `uint16_t`, wenn 65535 reicht → **4 Bytes weniger** (520 Bytes).

---

## 17. MemoryStats (memory_profiler.h)

- `size_t` auf ESP32 = 4 Byte; Arrays und Reihenfolge unkritisch. Kein offensichtliches Padding-Problem.

---

## Zusammenfassung – ESP32 (32-bit)

**Zielplattform:** 32-bit (ESP32 / ILP32). Pointer und `size_t` sind 4 Byte, Alignment ist 4 Byte (kein 8-Byte-Pointer-Alignment wie auf LP64). Alle folgenden Größen und Schlussfolgerungen gelten ausschließlich für dieses 32-bit-Target.

| Struct           | Größe (ESP32) | Padding      | Einsparung / Anmerkung                    |
|------------------|---------------|-------------|-------------------------------------------|
| **CljSlotRef**   | 12 B          | 2 B         | Reorder: kleine Felder vor Pointer (0 B gespart, bessere Nutzung) |
| **CljNamespace** | 28 B         | 3 B         | Reorder: `loaded` + Padding nutzen (0 B, Erweiterbarkeit) |
| **CljFunction**  | 28 B          | 3 B         | Reorder: `variadic_index` direkt nach base (0 B, Erweiterbarkeit) |
| **CLJException** | 460–468 B     | 0 B         | Nur durch kürzere Puffer (type/message/file) spürbar |
| **LineEditorState** | 524 B     | 3 B         | 0–4 B bei uint16_t für cursor_pos/length  |
| CljObject … CljMap, CljList, CljASTNode, Vector, HashMap | siehe Abschnitte | 0 B auf 32-bit | Kein Layout-Padding durch 32-bit-Alignment |

**Schlussfolgerungen (32-bit):** Weil wir auf 32-bit sind, haben Structs nur 4-Byte-Pointer und 4-Byte-Alignment; viele Structs haben **kein** Padding (im Gegensatz zu LP64). Trotzdem lohnt Reordering (Namespace, Function, SlotRef) für klare Nutzung von Lücken und spätere Erweiterung ohne Struct-Vergrößerung. Auf dem RAM-limitierten 32-bit-Target zählen zusätzlich: **CLJException**-Puffer kürzen und **LineEditorState**-Puffer/Feldtypen prüfen.

**malloc (ESP-IDF, 32-bit):** Pro Allokation kommen **8 Bytes TLSF-Overhead** (plus ggf. 12 Bytes bei CONFIG_HEAP_POISONING) und Aufrundung auf 4 Bytes dazu (Abschnitt 0). Kleine Structs (z. B. 4–12 Bytes) haben dadurch einen hohen Overhead pro Objekt; Pooling oder größere Blöcke reduzieren den Anteil. Die 8 Bytes Overhead gelten unverändert auf 32-bit (TLSF block_start_offset).

Nach Feldumstellung: `offsetof`/Layout-abhängigen Code und Unit-Tests prüfen; ESP32-Build und Tests laufen lassen.

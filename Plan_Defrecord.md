# Analyse und Optimierung von `defrecord` für den ESP32

## Problem
Aktuell erzeugt ein Aufruf von `(defrecord Name [felder])` in Clojure relativ viel Heap-Wachstum (ca. 1.7 KB pro Record). Für große Schema-Definitionen (wie in `gfx-scene`) summiert sich das zu massiven Werten, die auf dem ESP32 nicht tragbar sind.

## Ursache
Das Makro `defrecord` generiert bei der Ausführung umfangreichen Clojure-Code (Konstruktoren `->Type`, `map->Type` etc.), der geparst, kompiliert (AST) und als Closures im Speicher gehalten werden muss. Zudem belasten die erzeugten Symbole und der Makro-Aufruf selbst den internen Cache (`g_runtime.resolve_cache_epoch`) von tiny-clj.

## Lösungsvorschläge

### 1. Hardcodierte C-seitige Registrierung (Bereits umgesetzt für Basis-Records)
Für feste, interne Core-Typen, wie sie im `tiny-fx` Framework genutzt werden, ist es am effizientesten, die Typen direkt über C-APIs bekannt zu machen:
```c
CljPersistentVector *fv = make_vector(2, STRONG);
vector_conj_inplace(&fv, intern_symbol_global(":x"));
vector_conj_inplace(&fv, intern_symbol_global(":y"));
record_register_descriptor(intern_symbol_global("Foo"), fv);
```
**Vorteile:** 0 Byte Overhead beim Laden von Clojure-Code.
**Nachteile:** Weniger ergonomisch für den User; Änderungen erfordern Re-Kompilierung des C-Codes.

### 2. Nutzung der C-Native Primitives statt `defrecord`
Clojure-Seitig kann man auf das `defrecord`-Makro verzichten und stattdessen die rohen C-Bindings direkt nutzen:
```clojure
(record-register 'Foo '[:x :y])
(def ->Foo (fn ->Foo [x y] (record-create 'Foo [x y])))
```
Ein solcher direkter Aufruf allokiert lediglich ~112 Bytes (für die Definition der Closure `->Foo` und das Symbol `->Foo`) anstelle von 1.7 KB für das Makro.
**Vorteile:** Keine Makro-Expansion, minimale AST-Größe.
**Nachteile:** Man muss die Konstruktoren manuell tippen, wenn man sie braucht. Oft reicht aber auch nur das Schema und dann `record-from-map` bzw. `record-create` am Aufrufort (wie bei `edn->scene` erfolgreich demonstriert).

### 3. Makro `defrecord` "Light" (Nativ in C)
Wenn man die Ergonomie von `defrecord` behalten will, könnte man die Konstruktor-Generierung aus dem Clojure-Code nach C verschieben. Eine native Funktion `(native-defrecord Type [fields])`, die intern die `CljRecordDescriptor` anlegt und automatisch die Symbole `->Type` und `map->Type` generiert und an C-Funktionen bindet (ähnlich wie `make_named_func`).
**Vorteile:** Verbindet die Ergonomie von `defrecord` mit dem null-overhead von C.
**Aufwand:** Hoch (Erfordert Anpassung in `builtins.c` und dynamische Generierung von Symbol-Bindings auf zur Laufzeit erzeugte Funktionen, die eine dynamische Signatur haben).

### Empfehlung für das aktuelle Projekt
Für den aktuellen Stand (ESP32 Game Demo) hat sich **Ansatz 1 (und 2 in Kombination)** als der rettende Weg erwiesen. Die Grafik-Records werden in C initialisiert (`src/tiny_fx_gfx.c`), und das Clojure-Makro `defrecord` wird im User-Code einfach nicht verwendet, um den Heap sauber zu halten. Daten werden stattdessen generisch aus EDN via `record-from-map` instanziiert.

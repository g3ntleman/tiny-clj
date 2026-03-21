# Analyse und Optimierung von `defrecord` für den ESP32

## Problem
Aktuell erzeugt ein Aufruf von `(defrecord Name [felder])` in Clojure relativ viel Heap-Wachstum (ca. 1.7 KB pro Record). Für große Schema-Definitionen (wie in `gfx-scene` oder wenn User eigene Records definieren) summiert sich das massiv und gefährdet das RAM-Budget auf dem ESP32.

## Ursache
Das Makro `defrecord` (in `clojure/core.clj`) generiert bei der Auswertung viel Code:
```clojure
(defmacro defrecord [type-name fields]
  (let [ctor (symbol (str "->" (name type-name)))
        map-ctor (symbol (str "map->" (name type-name)))
        ...]
    (list 'do
          (list 'record-register ...)
          (list 'def ctor (list 'fn ctor fields ctor-body))
          (list 'def map-ctor (list 'fn map-ctor [m] map-body))
          ...)))
```
Dieser generierte Code (die `->Type` und `map->Type` Funktionen) muss geparst, als AST angelegt und schließlich als Clojure-Functions (`CljASTCall` / `CljClosure`) im Speicher gehalten werden. 

## Lösungsvorschläge

Hier sind drei Wege, wie man das für den ESP32 optimieren kann, absteigend sortiert nach Effizienz:

### 1. Hardcodierte C-seitige Registrierung (Für Framework-Core)
Für feste, interne Typen, wie sie im `tiny-fx` Framework genutzt werden (z.B. `Transform`, `FrameScene`), ist es am effizientesten, die Typen direkt über C-APIs bekannt zu machen.
**Beispiel:**
```c
CljPersistentVector *fv = make_vector(2, STRONG);
vector_conj_inplace(&fv, intern_symbol_global(":x"));
vector_conj_inplace(&fv, intern_symbol_global(":y"));
record_register_descriptor(intern_symbol_global("Foo"), fv);
```
* **Vorteile:** Exakt 0 Byte Overhead beim Laden von Clojure-Code.
* **Nachteile:** Unflexibel. Kann nicht vom User dynamisch aus Clojure heraus genutzt werden.

### 2. Nutzung der Nativ-APIs in Clojure (Das "defrecord Light" Pattern)
Wenn der User eigene Datenstrukturen definieren will, aber den Makro-Overhead scheut, kann er direkt die zugrundeliegenden Primitiven aufrufen. Das Makro kann komplett entfallen.
**Beispiel:**
Statt `(defrecord Foo [x y])` schreibt man:
```clojure
(record-register 'Foo '[:x :y])
(def ->Foo (fn ->Foo [x y] (record-create 'Foo [x y])))
```
Ein solcher Aufruf allokiert lediglich **~112 Bytes** Heap, anstatt der vollen **~1700 Bytes** des Makros. 
* **Vorteile:** Spart >90% RAM, benötigt keine C-Änderungen.
* **Nachteile:** Leicht reduzierter Komfort (man tippt Konstruktoren manuell, wenn man sie braucht). Oftmals braucht man die Konstruktoren aber gar nicht, wenn man Objekte generisch via `(record-from-map 'Foo {:x 1 :y 2})` aus EDN-Daten aufbaut (wie in `game-demo`).

### 3. Nativer Builtin-Befehl (Makro durch C-Funktion ersetzen)
Wenn man die Ergonomie von `defrecord` *unbedingt* beibehalten möchte, müsste man die Code-Generierung aus dem Clojure-Makro nach C verlagern. 
Man könnte ein Special-Form oder eine Native-Function in `builtins.c` einführen (z.B. `native_defrecord`), die:
1. `record_register_descriptor` aufruft.
2. Das Environment um C-native Funktionen für `->Type` und `map->Type` erweitert. Dies würde die Allokation von Clojure-Funktions-ASTs komplett einsparen.
* **Vorteile:** Voller Komfort bei geringem Speicherverbrauch.
* **Nachteile:** Hoher Implementierungsaufwand in C (dynamische Signatur-Validierung von C-Wrappern, Zuweisung von generierten Symbolen im Eval-Kontext). 

### Empfehlung
Für das aktuelle ESP32-Projekt fährst Du am besten mit **Vorschlag 1 für die Core-Engine** (haben wir gerade erfolgreich für `tiny-fx` umgesetzt) und **Vorschlag 2 für User-Space-Code**. 

Das heißt konkret: Wir empfehlen für ressourcenkritische ESP32-Anwendungen `defrecord` in Clojure nicht zu verwenden und stattdessen `record-register` und `record-from-map` zu nutzen.

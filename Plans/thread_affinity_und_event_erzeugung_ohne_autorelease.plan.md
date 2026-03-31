---
name: Thread-Affinity-Checks und Event-Erzeugung ohne Autorelease
overview: "Im DEBUG-Build soll Memory-Management auf Fremdthreads sofort auffallen: RETAIN/RELEASE/AUTORELEASE und make_*-basierte Objekt-Erzeugung sollen optional gegen den tiny-clj-Runtime-Thread validiert werden. Parallel dazu soll die Event-Pipeline so umgebaut werden, dass GPIO-, Sound-, Viewer- und Netzwerk-Callbacks auf C-Threads keine Clojure-Objekte mehr erzeugen oder autoreleasen muessen; stattdessen werden rohe C-Payloads an den Runtime-Thread uebergeben und erst dort in Maps/Records umgesetzt."
todos:
  - id: debug-thread-affinity-contract
    content: "DEBUG-Vertrag fuer tiny-clj-Thread-Affinity in Runtime/Memory-Schicht festlegen"
    status: pending
  - id: debug-memory-thread-checks
    content: "DEBUG-Checks fuer RETAIN/RELEASE/AUTORELEASE und make_*-Erzeugung auf Fremdthreads planen"
    status: pending
  - id: raw-event-ingress-design
    content: "Autorelease-freie Raw-Event-Ingress-API fuer Fremdthreads entwerfen"
    status: pending
  - id: producer-migration-slices
    content: "GPIO, Sound, Viewer und mDNS auf den Raw-Event-Ingress aufteilen und Migrationsreihenfolge planen"
    status: pending
  - id: thread-affinity-tests
    content: "Tests fuer DEBUG-Affinity-Checks und autorelease-freie Event-Erzeugung definieren"
    status: pending
isProject: false
---

# Plan: Thread-Affinity-Checks und Events ohne Autorelease

## Ziel

Zwei Probleme sollen gemeinsam adressiert werden:

1. Im `DEBUG`-Build soll sofort sichtbar werden, wenn tiny-clj-Memory-Management
   auf dem falschen Thread stattfindet.
2. Event-Produzenten auf C-Threads sollen keine Clojure-Objekte mehr erzeugen
   oder `AUTORELEASE` verwenden muessen.

Damit wird die aktuelle stillschweigende Fehlerklasse reduziert:

- Fremdthread erzeugt `CljObject`
- Fremdthread verwendet `AUTORELEASE`
- globale/nicht-thread-safe Memory-Infrastruktur wird trotzdem benutzt
- Fehler zeigt sich erst spaeter als RC-Drift, Pool-Korruption oder seltener Crash

## Recherche-Stand

### 1. Runtime-/Memory-Basis

- `runtime_init()` initialisiert frueh die Autorelease-Pool-Infrastruktur.
- `event_loop_run_next()` fuehrt Tasks auf dem tiny-clj-/Runloop-Thread in
  `WITH_AUTORELEASE_POOL(...)` aus.
- Die aktuelle Architektur geht also bereits implizit davon aus, dass
  Objekt-Erzeugung und Eval-Logik primaer auf dem Runtime-Thread stattfinden.

### 2. Event-Produzenten, die heute Clojure-Objekte ausserhalb des Runtime-Threads bauen

Aktuelle Kandidaten:

- `gpio_runtime_enqueue_watch_event()` in `src/gpio.c`
  - baut `event_map` direkt vor `event_loop_enqueue_ingress_call(...)`
- `notify_finished()` in `src/sound_engine.c`
  - baut `event_payload` direkt auf der Sound-Seite
- `mdns_emit_event()` in `src/builtins_mdns.c`
  - baut Maps/Vektoren/String-Objekte in einem Fremdthread-Kontext und schuetzt
    sich heute mit `WITH_AUTORELEASE_POOL(...)`
- Viewer-/Game-Demo-Pfad
  - `viewer_make_spatial_event()` baut `SpatialEvent`-/`Aabb`-Records
  - aktuell zwar im Host-Hauptpfad, aber architektonisch ist auch dort relevant,
    dass Event-Erzeugung nicht versehentlich auf Render-/Fremdthreads wandert

### 3. Warum Debug-Checks sinnvoll sind

Wenn im `DEBUG`-Build `retain()`, `release()`, `autorelease()` und/oder
Objekt-Erzeugung validieren, dass sie auf dem tiny-clj-Thread laufen, dann
fallen Fehler frueh und lokal auf, statt spaeter als:

- ueberlaufender RC
- Pool-Leaks
- sporadischer Cross-Thread-Crash
- schwer lesbarer Folgefehler in `event_loop_run_next()`

## Nicht-Ziel

Dieser Plan ersetzt nicht die bestehende bequeme pool-safe API fuer allgemeinen
Runtime-Code. Es geht nicht darum, `AUTORELEASE` repo-weit zu verbieten.

Es geht gezielt um:

- Fremdthreads
- plattformseitige Callback-Threads
- Tick-/Render-/Netzwerk-/ISR-nahe Produzenten

## Arbeitspaket 1: Debug-Vertrag fuer Thread-Affinity

### Ziel

Explizit festhalten, welcher Thread der tiny-clj-Memory-/Eval-Thread ist und
welche Operationen im `DEBUG`-Build nur dort erlaubt sind.

### Vorschlag

In der Runtime einen eindeutigen "owning thread" speichern, z. B.:

- bei `runtime_init()`
- oder spaeter explizit bei Runtime-Aktivierung / REPL-Start

Debug-Regel:

- erlaubt auf tiny-clj-Thread:
  - `make_*`
  - `AUTORELEASE`
  - reguläre `RETAIN`/`RELEASE` auf `CljObject`
  - Eval-/Namespace-/Map-/Record-/Vector-Erzeugung
- auf Fremdthreads standardmaessig verboten bzw. assertend:
  - `AUTORELEASE`
  - alle `make_*`, die heapbasierte `CljObject` erzeugen
  - optional auch `RETAIN`/`RELEASE`, wenn streng genug gewollt

### Entscheidungspunkt

Es gibt zwei sinnvolle Strengegrade:

1. **Strikt**
   - jede Memory-Operation auf falschem Thread assertet
2. **Pragmatisch**
   - `AUTORELEASE` und `make_*` assertieren
   - `RETAIN`/`RELEASE` nur warnen oder bleiben zunaechst erlaubt

Empfehlung fuer Start:

- `AUTORELEASE` + `make_*` strikt
- `RETAIN`/`RELEASE` optional in Phase 2 verschaerfen

## Arbeitspaket 2: Debug-Hooks in Memory-/Runtime-Schicht

### Betroffene Stellen

- `subjective-c/src/memory.c`
- `subjective-c/src/memory.h`
- `src/runtime.c`
- ggf. zentrale Allokations-/Objekt-Erzeugungsstellen

### Plan

1. Runtime speichert owning thread ID.
2. Zentrale Debug-Helfer:
   - `runtime_is_owner_thread()`
   - `runtime_assert_owner_thread(op_name)`
3. Diese Helfer werden in den kritischen Pfaden aufgerufen:
   - `autorelease()`
   - `retain()` / `release()` falls gewuenscht
   - oder noch besser in zentralen `alloc(..., type)`-/`make_*`-nahen Stellen

### Erwartetes Ergebnis

Ein Fremdthread, der aus Versehen `make_map`, `make_record_with_descriptor_values`,
`AUTORELEASE`, `make_string` oder aehnliches ausfuehrt, faellt im `DEBUG`-Build
sofort auf.

## Arbeitspaket 3: Raw-Event-Ingress ohne CljObject-Erzeugung

### Ziel

Fremdthreads enqueuen nur rohe C-Daten. Die eigentlichen Clojure-Maps/Records
entstehen erst spaeter auf dem Runtime-Thread.

### Architekturidee

Statt:

- Fremdthread baut `event_map` / `SpatialEvent` / `Aabb`
- Fremdthread ruft `event_loop_enqueue_ingress_call(fn, payload)`

neu:

- Fremdthread baut `struct RawEvent...`
- Fremdthread enqueued einen kleinen C-Job oder Slot-Eintrag
- Runtime-Thread drainiert diesen Job
- erst dort entsteht `Map`/`Record`
- erst dort wird Callback aufgerufen

### Wichtige Eigenschaft

Kein C-Thread ausser dem tiny-clj-Thread muss:

- `AUTORELEASE`
- `make_map`
- `make_vector`
- `make_record_with_descriptor_values`
- `make_string`

benutzen.

## Arbeitspaket 4: API-Form fuer den Raw-Ingress

### Option A: Generischer Raw-Event-Task

Ein Event-Loop-Ingress, der statt `ID arg` eine rohe C-Payload nimmt:

- Funktionszeiger fuer "build payload + invoke callback auf Runtime-Thread"
- roher `void *ctx` / kopierte Payload-Bytes

Beispielidee:

- enqueue `{builder_fn, callback_fn, raw_payload_bytes}`
- drain auf Runtime-Thread
- `builder_fn(raw_payload_bytes)` -> `ID event`
- `callback_fn(event)`

### Option B: Spezifische Producer-Queues

Je Quelle eine rohe Queue:

- GPIO raw watch events
- Sound finished raw events
- mDNS raw service events
- Spatial raw collision events

Dann drainiert der Runtime-Thread jede Queue und baut die passenden
Maps/Records direkt dort.

### Empfehlung

Zuerst ein kleiner generischer Kern, dann produktspezifische Builder:

- gemeinsame enqueue-/drain-Mechanik
- je Event-Typ ein eigener Builder auf dem Runtime-Thread

So bleibt die Schicht klein, ohne neue bloss weiterleitende Funktionen
einzufuehren.

## Arbeitspaket 5: Producer-Migration

### Slice 1: Sound

`notify_finished()` ist klein und gut isoliert.

Neu:

- Fremdthread/Tick-Seite enqueued rohe `track_id`
- Runtime-Thread baut `{:source :audio :kind :finished ...}`

### Slice 2: GPIO

`gpio_runtime_enqueue_watch_event()` soll keine Event-Map mehr bauen.

Neu:

- enqueue `{pin, level, callback}`
- Runtime-Thread baut die map-basierte Payload

### Slice 3: mDNS

`mdns_emit_event()` ist aktuell der deutlichste "Fremdthread mit Pool"-Fall.

Neu:

- rohes `MdnsResolvedService`-Snapshot enqueuen
- Runtime-Thread baut Map, String-Objekte und Address-Vektor
- `WITH_AUTORELEASE_POOL(...)` im Netzwerk-Callback soll danach entfallen

### Slice 4: Viewer/Spatial

Pruefen, ob dieser Pfad bereits sicher auf dem Runtime-/Main-Thread laeuft oder
ob auch dort ein Raw-Event-Modell sinnvoll ist. Der aktuelle Bedarf ist geringer
als bei GPIO/Sound/mDNS, aber fuer Architektur-Konsistenz wichtig.

## Arbeitspaket 6: Tests

### Debug-Affinity-Tests

- Test: `AUTORELEASE` auf Fremdthread assertet im `DEBUG`-Build
- Test: `make_map` / `make_string` / `make_record...` auf Fremdthread assertet
- optional: `RETAIN`/`RELEASE` auf Fremdthread assertet oder warnt

### Raw-Event-Tests

- Sound finished: roher Track-Event wird korrekt in Callback-Map umgesetzt
- GPIO: `{pin level}` wird auf Runtime-Thread korrekt in Map umgesetzt
- mDNS: rohes Service-Snapshot wird korrekt in Event-Map umgesetzt
- Event-Loop-Vertrag bleibt:
  - fire-and-forget
  - Callback-Return ignoriert
  - Payload-Felder unveraendert

### Regression

- Es gibt keinen Producer mehr, der in einem plattformfremden Callback-Thread
  `WITH_AUTORELEASE_POOL(...)` braucht, um Events fuer tiny-clj zu bauen.

## Risiken

- Ein zu strikter Fremdthread-Check in `retain()/release()` koennte vorhandene
  legitime technische Stellen aufdecken, die zunaechst nur migriert werden
  muessen.
- Raw-Payload-Queues brauchen klare Ownership-Regeln fuer Strings/Buffers, damit
  nicht einfach ein zweiter Lifetime-Fehler entsteht.
- mDNS ist der komplizierteste Slice, weil dort mehrere Strings und
  Adresslisten uebernommen werden muessen.

## Definition of Done

- `DEBUG` erkennt Memory-Management auf falschen Threads frueh und eindeutig.
- GPIO, Sound und mDNS erzeugen auf Fremdthreads keine Clojure-Objekte mehr.
- Event-Maps/Records entstehen nur noch auf dem tiny-clj-/Runtime-Thread.
- Die bisherige Event-Semantik fuer Clojure bleibt unveraendert.

---
name: Unified Thread Mechanism
overview: Vereinheitlicht alle Cross-Thread-Aufrufe auf einen klaren Interpreter-/Event-Loop-Mechanismus, test-first in kleinen Schritten. Jeder Schritt benennt konkrete Tests, Dateien, Zeilennummern und die exakte Änderung.
todos:
  - id: step-1a-dispatch-native-test
    content: "Test: dispatch_native auf Interpreter-Thread führt sofort aus, nicht über Queue"
    status: pending
  - id: step-1b-native-cleanup-on-discard-test
    content: "Test: Cleanup-Fn wird aufgerufen, wenn nativer Ingress-Slot bei event_loop_clear verworfen wird"
    status: pending
  - id: step-1c-native-clojure-ordering-test
    content: "Test: Natives und Clojure-Ingress mischen sich fair (bestehendes Ordering bleibt stabil)"
    status: pending
  - id: step-2-event-loop-contract
    content: Event-Loop-Ingress API minimal anpassen, damit die neuen Thread-Vertragstests grün werden
    status: pending
  - id: step-3a-sound-release-thread-test
    content: "Test: retained_obj-Release nach Stream-Ende passiert nicht auf Tick-Thread (deferred)"
    status: pending
  - id: step-3b-sound-shutdown-order-test
    content: "Test: sound_engine_shutdown gibt Streams erst frei, nachdem Tick-Thread gejoint wurde"
    status: pending
  - id: step-3c-sound-on-finished-race-test
    content: "Test: on_finished_fn-Update während laufendem Tick führt nicht zu Use-After-Free"
    status: pending
  - id: step-4a-sound-shutdown-fix
    content: "Fix: Shutdown-Reihenfolge ändern – erst pthread_join, dann stream_release/RELEASE"
    status: pending
  - id: step-4b-sound-on-finished-serialize
    content: "Fix: on_finished_fn über Atomic oder Cmd-Queue serialisieren"
    status: pending
  - id: step-4c-sound-release-fallback-remove
    content: "Fix: Fallback-RELEASE in sound_release_retained_obj entfernen – immer über Event-Loop deferrieren"
    status: pending
  - id: step-5-producer-alignment
    content: GPIO/FX-Producer prüfen und Docstrings an einheitlichen Vertrag angleichen
    status: pending
  - id: cleanup
    content: Sourcecode aufräumen – Debug-Code, temporäre Workarounds, tote Codepfade, überflüssige Kommentare entfernen
    status: pending
isProject: true
---

# Unified Thread Mechanism

## Plan-Ort (Single Source of Truth)

Dieser Plan lebt **nur** unter [`Plans/unified_thread_mechanism_77df5ecc.plan.md`](unified_thread_mechanism_77df5ecc.plan.md). Keine zweite Kopie unter `.cursor/plans/` pflegen.

## Ziel

Einen einzigen, gut verständlichen Mechanismus für Arbeit über Thread-Grenzen hinweg etablieren: Produzenten-Threads dürfen keine Clojure-/Heap-Arbeit direkt ausführen, sondern reichen sie immer an den Interpreter-/Event-Loop-Thread weiter.

## Leitentscheidung

`event_loop` wird zur einzigen offiziellen Cross-Thread-Brücke. Kein Producer-Thread darf direkt `RELEASE`/`RETAIN` auf Clj-Heap-Objekte ausführen.

## Test-First-Regel

Jede Teiländerung: zuerst Regressionstest, dann minimaler Production-Code-Fix, dann Testgruppe prüfen.

## Relevante Stellen

- Event-Loop / Ingress: [src/event_loop.h](src/event_loop.h), [src/event_loop.c](src/event_loop.c)
- Interpreter-Runloop: [src/fx_host_runloop.c](src/fx_host_runloop.c)
- Sound-Lifecycle: [src/sound_engine.h](src/sound_engine.h), [src/sound_engine.c](src/sound_engine.c), [src/sound_backend_host.c](src/sound_backend_host.c)
- Weitere Producer: [src/gpio.c](src/gpio.c), [src/fx_host_app.c](src/fx_host_app.c), [src/fx_spatial_dispatch.c](src/fx_spatial_dispatch.c)
- Regressionstests: [src/tests/test_event_loop_latency.c](src/tests/test_event_loop_latency.c), [src/tests/test_sound_engine.c](src/tests/test_sound_engine.c), [src/tests/test_breakout_runtime_startup.c](src/tests/test_breakout_runtime_startup.c)

---

## Schritt 1: Event-Loop-Vertrag als Tests festnageln

Datei: [src/tests/test_event_loop_latency.c](src/tests/test_event_loop_latency.c)

### 1a. `dispatch_native` auf Interpreter-Thread führt sofort aus

Bestehender Test `test_event_loop_native_ingress_callback_can_call_clojure_on_drain_thread` (Zeile ~273) deckt den **Fremd-Thread-Pfad** ab.

**Neuer Test:** `test_event_loop_dispatch_native_runs_inline_on_interpreter_thread`

- Vom Test-Thread (= Interpreter-Kontext) `event_loop_dispatch_native(callback, ctx, cleanup)` aufrufen
- Assert: `callback` wurde **synchron** ausgeführt (Zähler == 1)
- Assert: Ingress-Queue ist danach **leer** (`!event_loop_has_pending_tasks()`)
- Assert: `cleanup` wurde aufgerufen (Zähler == 1)

### 1b. Cleanup-Fn bei Verwerfen (`event_loop_clear`)

**Neuer Test:** `test_event_loop_clear_calls_native_cleanup`

- `event_loop_enqueue_ingress_native(callback, ctx, cleanup)` mit Zähler-Context
- Sofort `event_loop_clear()` aufrufen
- Assert: `cleanup` Zähler == 1 (Cleanup wurde gerufen)
- Assert: `callback` Zähler == 0 (Callback wurde **nicht** ausgeführt)

### 1c. Native/Clojure-Ordering

**Neuer Test:** `test_event_loop_clojure_task_runs_before_native_ingress`

- Clojure-Fn via `event_loop_enqueue(fn, NULL)` in die Task-Queue
- Nativen Ingress via `event_loop_enqueue_ingress_native(native_cb, ctx, NULL)`
- Erstes `event_loop_run_next(...)`: Assert, Clojure-Task läuft (setzt Atom `:clj`)
- Zweites `event_loop_run_next(...)`: Assert, native Callback läuft (setzt Flag)

**Prüfung:** `./build/unit-tests --test "test_event_loop_latency/*"`

---

## Schritt 2: Event-Loop minimal anpassen

Dateien: [src/event_loop.h](src/event_loop.h), [src/event_loop.c](src/event_loop.c)

Voraussichtlich **keine großen Änderungen** nötig – die Mechanik existiert schon. Falls `dispatch_native` im Test-Kontext unerwartet queued statt inline läuft: `!subjective_c_has_interpreter_thread()` Bedingung in Zeile ~844 prüfen.

Ziel: alle Tests aus Schritt 1 grün. Bestehende Tests grün.

---

## Schritt 3: Sound-Vertrag als Tests formulieren

Datei: [src/tests/test_sound_engine.c](src/tests/test_sound_engine.c)

### 3a. `retained_obj`-Release nicht auf Tick-Thread

**Bekanntes Problem:** `sound_release_retained_obj` (Zeile ~204 in `sound_engine.c`) hat einen Fallback-`RELEASE` auf dem Caller-Thread (= Tick-Thread), wenn `dispatch_native` fehlschlägt.

**Neuer Test:** `test_sound_retained_obj_release_deferred_to_event_loop`

- Sound-Engine init mit 1 Voice, Track abspielen
- Genug Ticks für Stream-Ende
- `event_loop_run_next` aufrufen
- Assert: `retained_obj` ist NULL (nach Drain freigegeben)
- Assert: kein ZOMBIE/Crash

### 3b. Shutdown-Reihenfolge: Join vor Release

**Bekanntes Problem:** `sound_engine_shutdown` (Zeile ~684) ruft `stream_release` (Zeile ~690) **vor** `sound_backend_shutdown`/`pthread_join` (Zeile ~699). Der Tick-Thread kann noch mitten in `sound_engine_tick` sein.

**Neuer Test:** `test_sound_shutdown_no_crash_while_playing`

- Sound-Engine init + Track abspielen + `sound_tick_start()`
- Sofort `sound_engine_shutdown()` aufrufen
- Assert: kein Crash (ZOMBIE/ASan)
- Assert: nach Shutdown `g_sound_engine.tick_running == false`

### 3c. `on_finished_fn` Data-Race

**Bekanntes Problem:** `on_finished_fn` wird vom Interpreter geschrieben (Zeile ~804) und vom Tick-Thread gelesen (Zeile ~620, ~645) **ohne Synchronisation** – klassisches Data Race.

**Neuer Test:** `test_sound_on_finished_update_while_ticking`

- Sound-Engine init + on_finished registrieren + Track abspielen
- Während Tick läuft: `sound_engine_on_finished(new_fn)` aufrufen
- Assert: kein Crash/Double-Release

**Prüfung:** `./build/unit-tests --test "test_sound_engine/*"`

---

## Schritt 4: Sound minimal nachziehen

### 4a. Shutdown-Reihenfolge fixen

Datei: [src/sound_engine.c](src/sound_engine.c), Funktion `sound_engine_shutdown` (Zeile ~684)

**Änderung:** Reihenfolge umdrehen:

1. `sound_backend_shutdown()` (beinhaltet `sound_tick_stop` + `pthread_join`)
2. Dann `stream_release` für alle Streams
3. Dann `RELEASE(on_finished_fn)`
4. Dann `memset`

### 4b. `on_finished_fn` serialisieren

Datei: [src/sound_engine.c](src/sound_engine.c), [src/sound_engine.h](src/sound_engine.h)

**Bevorzugte Variante (Atomic):** `on_finished_fn` als `_Atomic(ID)` deklarieren, in `sound_engine_on_finished` mit `atomic_store`, in `notify_finished`/`enqueue_finished` mit `atomic_load` lesen. Minimal-invasiv, kein neuer Cmd-Typ nötig.

**Alternative (Cmd-Queue):** Neuen `SOUND_CMD_SET_ON_FINISHED` in die existierende Cmd-Queue einführen, sodass der Tick-Thread den Callback selbst übernimmt. Sauberer, aber mehr Aufwand.

### 4c. Fallback-RELEASE in `sound_release_retained_obj` entfernen

Datei: [src/sound_engine.c](src/sound_engine.c), Funktion `sound_release_retained_obj` (Zeile ~204)

**Änderung:** Die Fallback-Pfade (Zeilen ~211–212 und ~222–223), die direkt `RELEASE(retained_obj)` aufrufen, entfernen. Bei malloc-Fehler oder vollem Ingress stattdessen loggen/warnen. Nach dem Shutdown-Fix (4a) ist der Tick-Thread beim Aufräumen garantiert beendet, sodass der Fallback nicht mehr nötig ist.

**Prüfung nach jedem Teilschritt:** `./build/unit-tests --test "test_sound_engine/*"`

---

## Schritt 5: Producer angleichen

### GPIO ([src/gpio.c](src/gpio.c))

Nutzt `event_loop_enqueue_ingress_call` (Zeilen ~332, ~675, ~733, ~745). Das ist **korrekt** für Clojure-Fn-Dispatch. **Keine Migration auf `enqueue_ingress_native`** nötig.

- Prüfen: Map-Allokation thread-safe (ISR-Kontext ESP32)?
- Docstrings/Kommentare an einheitlichen Vertrag angleichen

### FX Host App ([src/fx_host_app.c](src/fx_host_app.c))

Nutzt `enqueue_ingress_call` für Startup-Callback (Zeile ~1617) vom UI-Thread. Passt zum Vertrag. Nur Kommentar-Angleichung.

### fx_spatial_dispatch ([src/fx_spatial_dispatch.c](src/fx_spatial_dispatch.c))

Bereits auf `enqueue_ingress_native` umgestellt (Zeile ~169). Keine Änderung nötig.

**Prüfung:** `./build/unit-tests --test "test_event_loop_latency/*"` und `./build/unit-tests --test "test_breakout_runtime_startup/*"`

---

## Schritt 6: Cleanup

- Debug-Instrumentierung aus Tests entfernen
- Tote Codepfade in `event_loop.c` prüfen (z. B. ungenutzte Coalescing-Logik für native Slots)
- Docstrings in `event_loop.h` und `sound_engine.h` auf neuen Vertrag aktualisieren
- Langfristig prüfen: Braucht man `dispatch_native` noch oder reicht `enqueue_ingress_native` immer?

---

## Prüfstrategie pro Schritt

- Immer nur ein kleiner Vertrag pro Patch.
- Nach jedem Schritt nur die direkt betroffenen Testgruppen ausführen.
- Erst wenn diese stabil sind, den nächsten Vertrag angehen.
- Größere Integrationspfade wie [src/tests/test_breakout_runtime_startup.c](src/tests/test_breakout_runtime_startup.c) erst nach den lokalen Schritten als Absicherung nutzen.

## Risiken

- **dispatch_native** Mischsemantik: inline vs. queue – langfristig klären, ob nötig
- **ESP32-Backend:** Shutdown-Fix (4a) muss analog geprüft werden (`sound_backend_esp32.c`)
- **_Atomic(ID):** Setzt voraus, dass `ID` atomar speicherbar ist (32/64-Bit – in der Praxis ja)
- **Ingress-Queue-Kapazität:** 64 Slots; bei hoher Native-Last prüfen, ob das reicht

## Ergebnisbild

Nach der Umstellung gilt als einfache Regel:
„Jeder fremde Thread produziert nur Arbeit; nur der Interpreter-/Event-Loop-Thread darf Clojure, Clj-Heap-nahe Ownership und Native-Cleanup mit Runtime-Kontext ausführen.“

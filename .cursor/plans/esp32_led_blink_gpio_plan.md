---
name: "ESP32 LED Blink + GPIO-Architektur"
overview: "Schrittweiser Test-First-Plan: jede Phase startet mit roten Tests, danach minimale Implementierung, danach gruen + Regressionscheck."
last_validated: "2026-02-15"
todos:
  - id: gpio-write-primitive
    content: "Schritt 1 (TDD): Tests fuer gpio-write! rot -> Symbol/Lookup/Core-Stub + Host/ESP32-Implementierung -> gruen"
  - id: gpio-read-analog
    content: "Analog Input (ADC): gpio-read-analog / adc-read (pin) fuer Batterie/Sensoren; ESP32 ADC1, Rohwert oder mV"
  - id: gpio-analog-callback-threshold
    content: "Analog-Callbacks: optionaler threshold-Parameter; Callback nur wenn |delta| >= threshold (gegen Rauschen/Flatter)"
  - id: gpio-pwm
    content: "PWM Output (optional/spaeter): gpio-pwm! (pin duty) getrennte API wie Arduino analogWrite; ESP32 LEDC"
  - id: isr-safe-drain-bridge
    content: "Schritt 2 (TDD): ISR-Safety-Tests/Checks rot -> flag-only ISR + enqueue im Thread-Kontext -> gruen"
  - id: event-loop-latency-budget
    content: "Schritt 3 (TDD): Latenz-/Jitter-Tests rot -> adaptive Sleep-Strategie -> gruen"
  - id: perf-telemetry
    content: "Schritt 4 (TDD): Telemetrie-Tests rot -> Drop-Counter + Exposition -> gruen"
  - id: docs-and-web-game-spikes
    content: "Schritt 5: Doku finalisieren (idiomatisches Clojure + Web-Arcade-Optionen + Performance-Entscheidungen)"
  - id: clojure-fn-doc-metadata
    content: "Alle Clojure-Funktionen (GPIO, ggf. ADC/PWM) mit :doc-Metadata versehen (doc fn / IDE)"
---

# ESP32 LED Blink + performante GPIO-Architektur

## Kurzfazit der Validierung

Der Plan ist in der Grundrichtung richtig (einheitliche Event-Loop, Output als Primitive, Blink in Clojure), braucht aber drei harte Nachschaerfungen fuer Performance/Robustheit:

1. **ISR-Pfad korrigieren:** In `src/gpio_esp32.c` steht bereits der Hinweis, dass `event_loop_enqueue(...)` aus ISR "not strictly ISR-safe" ist. Das muss aus dem ISR-Pfad raus.
2. **Latenzbudget realistisch machen:** Im UART-REPL wird aktuell in der Input-Schleife fix `platform_sleep_ms(10)` genutzt (`esp32-idf/main/tinyclj_idf_run.c`). Das begrenzt Timer/GPIO-Reaktionszeit deutlich.
3. **Burst-Verhalten sichtbar machen:** Der GPIO-Ring ist fix (`GPIO_EVENT_RING_CAP = 32`), Drop-Faelle werden still verworfen. Ohne Counter ist das im Feld schlecht debugbar.

## Ist-Stand (gegen Code geprueft)

| Bereich | Status | Fundstelle |
|---|---|---|
| Event-Loop + Timer vorhanden | ja | `src/event_loop.c`, `src/repl.c` |
| GPIO Input (`gpio-watch`, `gpio-unwatch`) | ja | `src/gpio_esp32.c`, `src/clojure.core.clj` |
| GPIO Output (`gpio-write!`) | ja (Schritt 1 umgesetzt) | `src/symbol.c`, `src/builtins.c`, `src/gpio_esp32.c`, `src/clojure.core.clj` |
| GPIO PWM (`gpio-pwm!`) | nein | geplant als getrennte API (Arduino: analogWrite); ESP32 LEDC |
| Analog Input (ADC) | nein | geplant: `gpio-read-analog`/`adc-read` fuer Batterie/Sensoren; ESP32 ADC1 |
| Host-Stubs fuer GPIO | ja (watch/unwatch/simulate) | `src/builtins.c` |
| REPL-Loop auf ESP32 | ja, mit fixem Sleep | `esp32-idf/main/tinyclj_idf_run.c` |
| IDF-Driver-Dependency | bereits vorhanden | `esp32-idf/components/tinyclj/CMakeLists.txt` (`driver` in `REQUIRES`) |

## Architektur-Ziele (aktualisiert)

1. **LED-Blink aus Clojure** per `(schedule-periodic ...)` + `(gpio-write! ...)`, ohne zusaetzliche Blink-Task.
2. **Single-Threaded Semantik** fuer User-Code erhalten (Callbacks laufen in Event-Loop-Kontext).
3. **Low-Overhead Input/Output**: ISR bleibt minimal, keine unnötigen Queues/Allokationen auf Hot Paths.
4. **Spiele-taugliche Latenz**: 60 FPS (~16.67 ms) muss mit reproduzierbarem Jitter erreichbar sein.
5. **Shared-first ueber Plattformen**: moeglichst viel Logik in plattformneutralem Code (`src/*`) halten; plattformspezifischer Code nur als duenne Adapter-Schicht.

## Verbindliche Regel: Plattform-Sharing

- Neue Funktionalitaet wird standardmaessig zuerst plattformneutral entworfen.
- Plattformdateien (`*_esp32*`, `platform_*`) enthalten nur Hardware-/OS-spezifische Calls und minimale Glue-Logik.
- Keine Duplikation von Core-Logik zwischen Host und ESP32, wenn dieselbe Semantik in gemeinsamem Code abbildbar ist.
- Bei jeder Aenderung wird explizit geprueft:
  - Was kann in `src/` geteilt werden?
  - Was muss wirklich plattformspezifisch bleiben?
5. **Vollstaendige GPIO-Nutzung**: Digital Out, PWM Out und Analog In – API getrennt wie bei Arduino (digitalWrite / analogWrite / analogRead), keine gemeinsame „Konfigurations“-Funktion fuer alle Modi.
6. **Dokumentation**: Alle in diesem Plan neu eingefuehrten oder erweiterten Clojure-Funktionen (GPIO, ggf. Timer/Event-Loop) sind mit `:doc`-Metadata zu versehen (z.B. `^#^{:doc "..."} (defn gpio-write! ...)`), damit `(doc gpio-write!)` und Tooling sie anzeigen koennen.

## GPIO-API-Ueberblick (Digital, PWM, Analog)

| Richtung | Funktion (geplant) | Arduino-Entsprechung | ESP32-Backend |
|----------|---------------------|----------------------|---------------|
| Digital Out | `gpio-write!` (pin, level) | digitalWrite | driver/gpio |
| PWM Out | `gpio-pwm!` (pin, duty [0–255 oder 0–100%]) | analogWrite | LEDC |
| Analog In | `gpio-read-analog` (pin) oder `adc-read` (pin) | analogRead | ADC1/ADC2 |

- **Getrennte Funktionen** (wie Arduino): Kein gemeinsamer „pinMode“-Switch; welcher Aufruf genutzt wird, bestimmt das Verhalten. Auf demselben Pin kann zwischen digitalWrite und analogWrite gewechselt werden; analogRead betrifft ADC-faehige Pins (andere Kanalzuordnung).
- **Analog In – Use Cases:** Batterieanzeige (Spannungsteiler am ADC-Pin, z.B. Vector Handheld `VG_PIN_BAT_ADC` 35), Potis, Sensoren. Rueckgabe: Rohwert (z.B. 0–4095, 12 Bit) oder skaliert (mV); Kalibrierung/Mittelung optional (Handheld-Plan: „ADC calibration and moving-average battery reading“).
- **Optionale Threshold bei Analog-Callbacks:** Wenn ein Callback (z.B. bei periodischem ADC-Abruf oder „Analog-Watch“) den analogen Wert liefert, soll ein **optionaler Threshold** angegeben werden koennen. Nur wenn die Aenderung des Analogwerts gegenueber dem zuletzt gemeldeten Wert **mindestens** diesen Threshold erreicht (z.B. `|new - last| >= threshold`), wird der Callback aufgerufen. Liegt die Aenderung darunter, findet **kein** Callback statt. Damit lassen sich Rauschen und Flatter bei Batterie/Potis reduzieren, ohne in User-Code filtern zu muessen.

## Performance-Leitplanken

### 1) ISR-Minimalismus (must-have)

- ISR darf nur:
  - Pin-Level lesen,
  - Event in Ring schreiben,
  - ein atomisches "drain requested"-Flag setzen.
- **Kein** `event_loop_enqueue` aus ISR.
- Enqueue des Drain-Tasks erfolgt spaeter im normalen Thread-Kontext (Event-Loop-Tick).

Erwarteter Effekt:
- weniger ISR-Risiko, geringerer Interrupt-Overhead, robustere Laufzeit unter Last.

### 2) Event-Loop-Latenz (must-have)

- Heute: fix `sleep(10ms)` in der UART-REPL-Input-Schleife.
- Neu: adaptiv schlafen:
  - `0-1ms`, wenn Task-Queue nicht leer oder Timer "bald faellig",
  - laenger schlafen nur im echten Idle.
- Optional: `event_loop_time_until_next_timer_ms()` als Helper.

Erwarteter Effekt:
- deutlich weniger Tick-Jitter fuer `schedule-periodic` bei 16ms/33ms Spiel-Loops.

### 3) Burst-/Backpressure-Strategie (should-have)

- Ringoverflow zaehlen (`gpio_event_drop_count`).
- Optional Coalescing je Pin (nur "letzter Zustand" fuer sehr schnelle Flanken), wenn Use-Case das zulaesst.
- Channel-Fassade (`tiny-clj.gpio/gpio-channel`) bleibt ergonomisch, aber fuer High-Rate-Input nicht die Default-Empfehlung.

Erwarteter Effekt:
- messbare Grenzen statt "silent failure", besseres Tuning.

## Schrittfolge (Test-First)

### Schritt 0: Baseline sichern

- Aktuellen Testzustand dokumentieren:
  - `./build/unit-tests --test 'test_plan_trackA_scripts/*'`
  - `./build/unit-tests --test 'test_timer/*'`
- Ziel: Vor dem Umbau eine bekannte gruen/rot-Basis haben.

#### Baseline-Protokoll (2026-02-15)

1. `./build/unit-tests --test 'test_plan_trackA_scripts/*'`
   - Ergebnis: **OK** (`4 Tests, 0 Failures, 0 Ignored`)
   - Laufzeit: `0.123s`
2. `./build/unit-tests --test 'test_timer/*'`
   - Ergebnis: **OK** (`15 Tests, 0 Failures, 0 Ignored`)
   - Laufzeit: `0.452s`

### Schritt 1: `gpio-write!` (TDD)

- **Rot**:
  - Neue Tests anlegen fuer:
    - native lookup findet `gpio-write!`
    - `(gpio-write! pin level)` ist aus `user` aufrufbar
    - Host-Stubs verhalten sich stabil (nil-return, Arity-Checks)
- **Gruen**:
  - `src/symbol.h`/`src/symbol.c`: `sym_gpio_write_data`
  - `src/builtins.c`: native lookup + Host-Stub
  - `src/gpio_esp32.h`/`src/gpio_esp32.c`: ESP32-Implementierung
  - `src/clojure.core.clj`: `(defn gpio-write! [pin level] :native)` mit `^#^{:doc "..."}` (siehe Architektur-Ziel Dokumentation)
- **Regression**:
  - `./build/unit-tests --test 'test_gpio_write/*'`
  - `./build/unit-tests --test 'test_plan_trackA_gpio_smoke_script'`

#### Schritt-1-Protokoll (2026-02-15)

1. **Rot bestaetigt**:
   - `./build/unit-tests --test 'test_gpio_write/*'`
   - Anfangsstatus: FAIL (Lookup/Resolution fehlten)
2. **Gruen hergestellt** (umgesetzt):
   - `src/tests/test_gpio_write.c` angelegt
   - `CMakeLists.txt` erweitert (`test_gpio_write.c` in `unit-tests`)
   - `src/symbol.h`/`src/symbol.c`: `sym_gpio_write_data`
   - `src/builtins.c`: native lookup + Host-Stub `native_gpio_write`
   - `src/gpio_esp32.h`/`src/gpio_esp32.c`: ESP32-Implementierung `native_gpio_write`
   - `src/clojure.core.clj`: `gpio-write!` mit `^#^{:doc "..."}`
3. **Regression gruen**:
   - `./build/unit-tests --test 'test_gpio_write/*'` -> **OK** (`4 Tests, 0 Failures`)
   - `./build/unit-tests --test 'test_plan_trackA_gpio_smoke_script'` -> **OK** (`1 Test, 0 Failures`)
   - Zusatzcheck: `./build/unit-tests --test 'test_native_lookup/*'` -> **OK** (`5 Tests, 0 Failures`)

### Schritt 2: ISR-safe Drain-Bridge (TDD)

- **Rot**:
  - Test/Check einbauen, dass ISR keine nicht-ISR-safe Queue-API direkt nutzt.
  - Optional: statischer Guard/Kommentar-Test fuer den Pfad.
- **Gruen**:
  - ISR nur Ring-Push + `drain_requested` Flag.
  - Drain-Task enqueue nur im Thread-Kontext.
  - Semantik von `gpio-watch`/`gpio-unwatch` unveraendert.
- **Regression**:
  - `./build/unit-tests --test 'test_plan_trackA_gpio_smoke_script'`
  - `./build/unit-tests --test 'test_go_blocks/*'` (Event-Loop-Verhalten)

#### Schritt-2-Protokoll (2026-02-15)

1. **Rot bestaetigt**:
   - Neue Vertrags-Tests `src/tests/test_gpio_architecture_contract.c` angelegt.
   - Vor Implementierung waren die geforderten Strukturen (`drain_requested` + Poll-Funktion) nicht vorhanden.
2. **Gruen hergestellt** (umgesetzt):
   - `src/gpio_esp32.c`:
     - ISR setzt jetzt nur `g_gpio_drain_requested` (Flag-only).
     - Neuer Thread-Kontext-Hook: `gpio_esp32_poll_drain()`.
     - Direkter ISR-Scheduling-Helfer entfernt (`gpio_schedule_drain_from_isr`).
   - `src/gpio_esp32.h`: neue Hook-Deklaration `gpio_esp32_poll_drain()`.
   - `src/event_loop.c`: ruft `gpio_esp32_poll_drain()` in `event_loop_run_next()` unter `ESP32_BUILD`.
3. **Regression gruen**:
   - `./build/unit-tests --test 'test_gpio_architecture_contract*'` -> **OK** (`2 Tests, 0 Failures`)
   - `./build/unit-tests --test 'test_plan_trackA_scripts*'` -> **OK** (`4 Tests, 0 Failures`)
   - `./build/unit-tests --test 'test_go_blocks*'` -> **OK** (`13 Tests, 0 Failures`)

### Schritt 3: Latenz/Jitter reduzieren (TDD)

- **Rot**:
  - Test/Probe fuer Sleep/Jitter-Budget (insb. 16ms-Loop) definieren.
- **Gruen**:
  - `platform_sleep_ms(10)` in UART-Loop auf adaptive Strategie umstellen.
  - Optional Helper in Event-Loop: naechster Timer in ms / pending-work.
- **Regression**:
  - `./build/unit-tests --test 'test_timer/*'`
  - ESP32-Laufprobe mit periodischem 16ms-Timer.

#### Schritt-3-Protokoll (2026-02-15)

1. **Rot bestaetigt**:
   - Neue Tests `src/tests/test_event_loop_latency.c` angelegt (fehlende API).
2. **Gruen hergestellt** (umgesetzt):
   - `src/event_loop.h`/`src/event_loop.c`:
     - `event_loop_has_pending_tasks()`
     - `event_loop_time_until_next_timer_ms()`
   - `esp32-idf/main/tinyclj_idf_run.c`:
     - fixe Pause `platform_sleep_ms(10)` ersetzt durch adaptive Strategie:
       - `1ms` bei pending work
       - sonst timer-nahe Werte `<10ms`
       - fallback `10ms` im Idle
3. **Regression gruen**:
   - `./build/unit-tests --test 'test_event_loop_latency*'` -> **OK** (`3 Tests, 0 Failures`)
   - `./build/unit-tests --test 'test_timer*'` -> **OK** (`15 Tests, 0 Failures`)

### Schritt 4: Burst-Telemetrie (TDD)

- **Rot**:
  - Test fuer Ringoverflow-Zaehler/Expose-Funktion.
- **Gruen**:
  - `gpio_event_drop_count` erhoehen bei Ring-full.
  - Counter via Runtime-Stats oder dediziertes API auslesbar machen.
- **Regression**:
  - `./build/unit-tests --test 'test_runtime_stats/*'`
  - GPIO-Burst-Testskript.

#### Schritt-4-Protokoll (2026-02-15)

1. **Rot bestaetigt**:
   - Neuer Runtime-Stats-Test fuer `:gpio-event-drops` (zunaechst fehlender Key).
2. **Gruen hergestellt** (umgesetzt):
   - `src/gpio_esp32.c`:
     - `g_gpio_event_drop_count` eingefuehrt.
     - Counter erhoeht bei Ringoverflow.
     - Getter `gpio_esp32_get_event_drop_count()` eingefuehrt.
   - `src/gpio_esp32.h`: Getter-Deklaration.
   - `src/builtins.c`: `tiny-clj.runtime/stats` liefert jetzt `:gpio-event-drops` (Host=0, ESP32=realer Counter).
   - `src/tiny-clj.runtime.clj`: Doku um `:gpio-event-drops` erweitert.
3. **Regression gruen**:
   - `./build/unit-tests --test 'test_runtime_stats/runtime_stats_gpio_event_drops_present'` -> **OK**
   - `./build/unit-tests --test 'test_runtime_stats*'` -> **OK** (`17 Tests, 0 Failures`)

### Schritt 5: Doku + Abschluss

- Architekturentscheidung dokumentieren:
  - callback-first fuer geringe Latenz
  - channel-wrapper fuer Ergonomie
  - keine LED-Sonder-API, nur `gpio-write!`
  - Digital / PWM / Analog als getrennte Primitive (Arduino-Vorbild)
  - alle neuen/geaenderten Clojure-Funktionen mit `:doc` versehen
- Abschluss-Checkliste:
  - Host-Tests gruen
  - `idf.py build` gruen
  - Board-Demo: Blink start/stop und keine regressiven Drops im Normalbetrieb

#### Schritt-5-Abschluss (2026-02-15)

1. **Plan bis Ende ausgefuehrt (Host/TDD-Seite)**:
   - Schritte 0 bis 4 umgesetzt und jeweils mit Tests protokolliert.
2. **Host-Regression gesammelt gruen**:
   - `test_gpio_write*`: **OK**
   - `test_gpio_architecture_contract*`: **OK**
   - `test_event_loop_latency*`: **OK**
   - `test_plan_trackA_scripts*`: **OK**
   - `test_go_blocks*`: **OK**
   - `test_timer*`: **OK**
   - `test_runtime_stats*`: **OK**
3. **Offen fuer Board-Validierung**:
   - `idf.py build` wurde erfolgreich ueber `./build_idf.sh` ausgefuehrt (App-Binary gebaut).
   - Reale ESP32-Laufprobe (Blink/Jitter/Drop-Counter unter Last) bleibt noch ausstehend.

### Erweiterungen (nach Schritt 5): Analog In + PWM Out

- **Analog Input (ADC):**
  - Primitive: `gpio-read-analog` (pin) oder `adc-read` (pin). Rueckgabe: Fixnum (Rohwert 0–4095 oder skaliert in mV; Semantik festlegen). Mit `:doc`-Metadata.
  - ESP32: ADC1/ADC2, Pin-zu-Kanal-Mapping (z.B. GPIO35 → ADC1_CH7). Optional: Kalibrierung, Mittelung (vgl. Vector Handheld `VG_BAT_ADC_SAMPLES`).
  - Use Case: Batterieanzeige aus Clojure, z.B. `(gpio-read-analog 35)` mit Teiler/Faktor zur Anzeige.
  - **Analog-Callbacks mit optionalem Threshold:** Falls ein API-Element Callbacks bei Analogwerten ausloest (z.B. `gpio-watch-analog` mit Polling oder periodischer ADC + Callback): optionaler Parameter **threshold** (Fixnum, gleiche Einheit wie der Wert). Callback wird nur ausgefuehrt, wenn `|aktueller_wert - zuletzt_gemeldeter_wert| >= threshold`. Kein Callback bei Aenderung unter dem Threshold – reduziert Rauschen/Flatter ohne User-seitiges Filtern.
- **PWM Output:**
  - Primitive: `gpio-pwm!` (pin, duty) – getrennt von `gpio-write!` (wie Arduino analogWrite vs digitalWrite). Duty z.B. 0–255 oder 0–100%. Mit `:doc`-Metadata.
  - ESP32: LEDC (Timer/Kanal); Frequenz optional konfigurierbar oder Default (z.B. 5000 Hz).
  - Use Case: dimmbare LED, Motoren, Servos.

## Idiomatisches Clojure (fuer diese Architektur)

Aus den offiziellen Clojure-Referenzen und Contrib-Guidelines lassen sich fuer dieses Vorhaben diese Regeln ableiten:

1. **Pure Transformationen bevorzugen** und mutable State auf den Rand beschraenken.
2. **Pipelines mit `->` / `->>`** statt verschachtelter Calls fuer lesbaren Dataflow.
3. **Atoms nur fuer unabhängigen, synchronen State**; Funktionen in `swap!` muessen nebenwirkungsfrei sein.
4. **Performante Primitive exponieren, Komfort in Libs komponieren** (hier: `gpio-write!` minimal, `tiny-clj.gpio` als Wrapper-Schicht).

Konkrete Ableitung:
- `gpio-write!` bleibt klein und direkt; jede solche Primitive traegt `^#^{:doc "..."}` fuer (doc fn) und IDE-Support.
- Hoehere Semantik (Blinker, Game-State-Maschinen, Debounce) bleibt in Clojure-Funktionen; auch diese mit :doc versehen, wo sie oeffentlich sind.

## Ideen fuer Clojure-Arcade-Games im Web (Recherche)

### Option 1: `play-cljc` (Clojure + ClojureScript)

- `play-cljc` ist explizit fuer Clojure/ClojureScript Games ausgelegt.
- Offizielle Example-Sammlung zeigt direkt brauchbare Arcade-Richtungen:
  - `basic-bird`
  - `dungeon-crawler`
  - `super-koalio`

### Option 2: Quil mit ClojureScript

- Quil unterstuetzt ClojureScript.
- Processing/p5js-kompatibler Zeichen-/Sketch-Ansatz ist gut fuer schnelle Prototypen (Setup/Draw-Loop).

### Option 3: Phaser via ClojureScript-Interop (npm)

- Phaser ist aktiv gepflegt und breit eingesetzt.
- Mit `shadow-cljs` ist npm-Interop direkt moeglich (moderne, belastbare Route).
- Historische Wrapper (`phzr`, `cljsjs/phaser`) wirken veraltet und sollten nur bewusst/legacy genutzt werden.

## Uebernehmen / Nicht uebernehmen

### Uebernehmen

1. **Kleine Native-Primitives, Komposition in Clojure**
   - `gpio-write!` bleibt schlank und performant; Blinker/Debounce/Game-Logik in Clojure.
2. **:doc fuer alle oeffentlichen Funktionen**
   - Jede neue oder geaenderte Clojure-Funktion (GPIO, ADC, PWM, Libs) mit `^#^{:doc "..."}`; (doc fn) und IDE nutzbar.
3. **Callback-first fuer Low-Latency**
   - `gpio-watch` als Default fuer schnelle Reaktionen, Channel-Wrapper nur bei Bedarf.
4. **Idiomatischer State mit Atoms**
   - Reine Update-Funktionen in `swap!`; Seiteneffekte am Rand (GPIO/Timer).
5. **Game-Loop-Muster aus `play-cljc`**
   - Trennung von Update/Render/Input, deterministische Tick-Funktionen.
6. **Modernes CLJS-Interop-Muster**
   - Fuer Web-Spikes: `shadow-cljs` + npm als Referenzansatz.

### Nicht uebernehmen

1. **Veraltete Wrapper als Default**
   - `phzr`/`cljsjs/phaser` nicht als Standardpfad fuer neue Arbeit.
2. **LED-Sonder-API**
   - Keine zusaetzliche LED-spezifische Ebene; LED bleibt ein normaler GPIO-Pin via `gpio-write!`.
3. **Gemeinsame „Write“-Funktion fuer Digital und PWM**
   - Wie Arduino: getrennte `gpio-write!` (digital) und `gpio-pwm!` (PWM); kein Modus-Parameter in einer Funktion.
4. **Channel-only als Vorgabe**
   - Channel-Fassade bleibt optional, nicht Pflicht auf dem Hot Path.

## Anschluss Vector Handheld

- Battery-ADC-Pin (`VG_PIN_BAT_ADC` 35), Teiler und Kalibrierung sind im Handheld-Plan bereits als C/Config vorgesehen. Mit `gpio-read-analog` (bzw. `adc-read`) kann die Batterie-Anzeige und Low-Battery-Logik auch von Clojure aus gelesen werden (einmal pro Frame oder per Timer).

## Verifikation (nach Umsetzung)

1. Host:
   - Unit-Tests laufen weiterhin gruen.
2. ESP32 Build:
   - `idf.py build` erfolgreich.
3. Runtime:
   - Blink-Demo startet/stoppt per `cancel-timer`.
4. Lasttests:
   - GPIO-Burst testet Ringoverflow-Counter.
   - 16ms-Game-Tick ueber mehrere Minuten messen (Jitter/Drift dokumentieren).

## Beispiel (idiomatisch + low overhead)

```clojure
(defonce led-state (atom 0))

(defn toggle-led! [pin]
  (let [next (if (zero? @led-state) 1 0)]
    (reset! led-state next)
    (gpio-write! pin next)))

(def blink-id
  (schedule-periodic 0 500
    {:id :status-led
     :fn (fn [] (toggle-led! 2))}))

;; stop:
;; (cancel-timer :status-led)
```

## Quellen (Web)

- Clojure Functions Guide: https://clojure.org/guides/learn/functions
- Clojure Threading Macros Guide: https://clojure.org/guides/threading_macros
- Clojure Atoms Reference: https://clojure.org/reference/atoms
- Clojure Transducers Reference: https://clojure.org/reference/transducers
- Clojure Contrib Coding Guidelines: https://clojure.org/community/contrib_howto
- ClojureScript Dependencies: https://clojurescript.org/reference/dependencies
- shadow-cljs User Guide: https://shadow-cljs.github.io/docs/UsersGuide.html
- play-cljc: https://github.com/oakes/play-cljc
- play-cljc-examples: https://github.com/oakes/play-cljc-examples
- Quil: https://github.com/quil/quil
- Phaser: https://github.com/phaserjs/phaser
- phzr (historisch): https://clojars.org/phzr
- cljsjs/phaser (historisch): https://clojars.org/cljsjs/phaser

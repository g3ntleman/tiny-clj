# ESP32 Sound Backend: Minimales Cleanup

## Overview

Minimale Anpassung des ESP32-Sound-Backends: `sound_engine_advance_ticks(due)` durch per-tick for-Loop ersetzen fuer Konsistenz mit dem Host-Backend. Die bestehende esp_timer-Architektur (event-basiertes Scheduling via `ticks_until_deadline()`, Sleep/Wake via Timer-Stop/Start) bleibt erhalten.

## Status

- Teilweise erledigt (Stand 31.03.2026).
- `sound_timer_callback` in `src/sound_backend_esp32.c` iteriert inzwischen pro faelligem Tick ueber `sound_engine_tick()` und nutzt kein `sound_engine_advance_ticks(due)` mehr.
- Gezielte Host-Regressionen sind in diesem Audit gruen: `./build/unit-tests --test 'test_sound_engine/*' --quiet` lief mit `111 Tests, 0 Failures`.
- In dieser Release-Runde wurde das geplante minimale Backend-Cleanup nachgezogen: Kommentarstand auf one-shot/`VG_SOUND_TICK_MS` aktualisiert, Timer-Zustand im Init/Shutdown explizit zurueckgesetzt und ein Source-Contract gegen Rueckfall auf `sound_engine_advance_ticks(...)` ergaenzt.
- `./build/unit-tests --test 'test_gpio_architecture_contract/*' --quiet` lief dazu gruen (`18 Tests, 0 Failures`).
- Offen bleibt nur noch der urspruengliche Vollsuite-Nachweis. Der aktuelle Vollsuite-Lauf endete allerdings in einem offenbar breiteren Parse/OOM-Fehler ausserhalb dieses kleinen Sound-Backends.

## Todos

- [x] `sound_timer_callback` in `src/sound_backend_esp32.c`: `sound_engine_advance_ticks(due)` durch `for`-Loop mit `sound_engine_tick()` ersetzen
- [ ] Host-Build + vollstaendige Unit-Tests (urspruenglicher Abschluss-Gate; gezielte Sound-/Architektur-Suites sind gruen, Vollsuite derzeit separat blockiert)
- [x] Sourcecode aufraeumen -- tote Codepfade, ueberfluessige Kommentare entfernen

## Release-Audit 31.03.2026

- Verifiziert:
    - `build-tests` baut sauber.
    - `test_sound_engine/*` bleibt gruen.
    - `test_gpio_architecture_contract/*` bestaetigt weiter die shared-GPIO/PWM-Architektur und den per-tick-Callbackpfad.
- Noch nicht als abgeschlossen markiert:
    - Ein kompletter `./build/unit-tests`-Lauf scheiterte in diesem Audit mit einem separaten Parse-/OOM-Fall; das ist als breiteres Suite-Thema zu behandeln, nicht als Sound-Backend-Regressionssignal.

## Aenderung in `src/sound_backend_esp32.c`

In `sound_timer_callback`, Zeile 112:

```c
// Vorher:
sound_engine_advance_ticks(due);

// Nachher:
for (uint32_t i = 0; i < due; i++) {
    sound_engine_tick();
}
```

Alles andere bleibt: `esp_timer` one-shot, `ticks_until_deadline()`-basiertes Scheduling, Sleep/Wake via Timer-Stop/Start.

## Begruendung

Die bestehende ESP32-Architektur ist bereits event-basiert und CPU-freundlich:
- Timer feuert nur bei `ticks_until_deadline()` (nicht per-tick)
- LEDC-Hardware haelt PWM-State autonom
- `sound_tick_sleep()` stoppt Timer (0% CPU)
- `sound_tick_kick()` startet Timer sofort neu (us-Praezision)

Die einzige Inkonsistenz zum Host-Backend war `advance_ticks` (Batch) vs per-tick Loop. Diese wird hiermit behoben.

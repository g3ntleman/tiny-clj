# ESP32 Sound Backend: Minimales Cleanup

## Overview

Minimale Anpassung des ESP32-Sound-Backends: `sound_engine_advance_ticks(due)` durch per-tick for-Loop ersetzen fuer Konsistenz mit dem Host-Backend. Die bestehende esp_timer-Architektur (event-basiertes Scheduling via `ticks_until_deadline()`, Sleep/Wake via Timer-Stop/Start) bleibt erhalten.

## Todos

- [ ] `sound_timer_callback` in `src/sound_backend_esp32.c`: `sound_engine_advance_ticks(due)` durch `for`-Loop mit `sound_engine_tick()` ersetzen
- [ ] Host-Build + vollstaendige Unit-Tests (1931+ Tests, 0 Failures)
- [ ] Sourcecode aufraeumen -- tote Codepfade, ueberfluessige Kommentare entfernen

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

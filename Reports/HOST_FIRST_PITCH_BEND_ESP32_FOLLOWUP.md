# Host-First Pitch Bend ESP32 Follow-up

## Ziel

Die Host-DEBUG-Helfer definieren eine kleine, uebertragbare Semantik fuer kontinuierliche Tonhoehenbewegung:

- linearer Einzelramp: `start-freq -> end-freq` ueber `duration-ms`
- Ramp-Noise: Folge kurzer linearer Teilrampen innerhalb eines Frequenzbandes

## Empfohlene Uebertragung auf den ESP32/Piezo-Pfad

1. Neue DEBUG-only oder Engine-interne Ramp-Command-Semantik definieren.
2. Im Tick-Pfad keine diskreten Noten fuer diese Faelle erzeugen, sondern pro Tick die Ziel-Frequenz aus einer Ramp-Struktur ableiten.
3. Die bestehende Piezo/LEDC-Ausgabe weiter nur mit `freq_hz` + `volume` fuettern.
4. Ramp-Noise als wiederholte kurze Segmente mit festem `hop-ms` und begrenztem Frequenzband modellieren.

## Minimales Datenmodell

```text
type: linear-ramp | ramp-noise
start_freq_hz
end_freq_hz / max_freq_hz
min_freq_hz
duration_ms
hop_ms
volume
seed
```

## Integrationsreihenfolge

1. Zuerst Host-DEBUG-Semantik stabil halten.
2. Dann kleine Engine-interne Ramp-Structs in `sound_engine.*` einfuehren.
3. Danach Tick-seitige Frequenzinterpolation fuer ESP32 aktivieren.
4. Erst zum Schluss DSL-Zucker oder Produktions-API darauf aufbauen.

## Risiken

- ESP32 arbeitet tick-basiert statt sample-basiert; schnelle Ramps klingen dadurch grober.
- Zu kleine `hop-ms` koennen auf dem Piezo-Pfad CPU- und Timer-Druck erhoehen.
- Fuer Musik muessen Ramp-Start/Stop-Semantik und Gate-Verhalten klar bleiben, damit bestehende Track-Wiedergabe nicht regressiert.

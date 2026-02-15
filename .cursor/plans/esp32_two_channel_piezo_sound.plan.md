---
name: ESP32 Mehrkanal-Piezo-Sound (trk1 / MUS-lite)
overview: Songs bleiben in Clojure (DSL/Maps), werden in kompaktes trk1-ByteArray kompiliert und als Datei in der FS-Emulation gespeichert. C streamt trk1 direkt (MUS-lite), Kanalanzahl ist konfigurierbar.
todos:
  - id: architecture-lock
    content: "Architektur fixieren: Clojure authoring + compile-to-trk1, C Audio-Core streamt direkt ohne Full-Decode."
    status: pending
  - id: trk1-format
    content: trk1 bytegenau finalisieren (Header, Event-Bitfelder, Varint-Delay, Validierungsregeln).
    status: pending
  - id: ownership-contract
    content: "Ownership-Vertrag umsetzen: audio-load-track! macht RETAIN, unload/shutdown macht RELEASE, kein RC im Tick."
    status: pending
  - id: native-api-wiring
    content: "Native API verdrahten: audio-load-track!, audio-unload-track!, audio-play-music!, audio-stop-track!, audio-stop-music!, audio-play-sfx!, audio-stop-all!, audio-set-track-volume!, audio-set-music-volume!, audio-take-finished!."
    status: pending
  - id: ledc-multichannel
    content: LEDC fuer N Kanaele konfigurieren (Timer/Channel Mapping validieren, Board-Default 2 Outputs).
    status: pending
  - id: scheduler-and-stream-parser
    content: 1ms Tick + direkter trk1 Stream-Parser + bounded command/finished queues implementieren.
    status: pending
  - id: storage-fs-emulation
    content: "Persistenz ueber FS-Emulation: Composer schreibt .trk1-Dateien; Runtime-ID ist Symbol/Keyword, gemappt auf Dateiname."
    status: pending
  - id: sfx-oneshot-policy
    content: "SFX-Policy definieren: kein separates SFX-Format; SFX nutzen ebenfalls trk1 + One-Shot-Semantik mit Prioritaet/Voice-Steal/Drop-Regeln."
    status: pending
  - id: tests-and-soak
    content: Host-TDD, ESP32 Smoke + 30min Soak, Telemetrie-Counter validieren.
    status: pending
isProject: false
---

# ESP32 Mehrkanal-Piezo-Sound fuer Vector Handheld

## 1) Zielbild

- **Songs bleiben in Clojure** (lesbar/editierbar als DSL/Maps).
- Laufzeit nutzt **kompaktes ByteArray (`trk1`)**.
- `trk1` wird als **Datei in der FS-Emulation** gespeichert.
- C-Engine spielt als **MUS-lite Stream direkt** (kein Full-Decode in große Event-Tabellen).
- **Mehrkanal**: Output-Anzahl ist konfigurierbar (`1..N`), Board-Default aktuell 2.
- Timing bleibt deterministisch im C-Core (1ms Tick, kein Audio-Timing im Interpreter).
- **SFX** werden als **One-Shot** einmalig abgespielt, auch waehrend Musik laeuft.
- Im MVP ist die Runtime-Identitaet ein **Symbol/Keyword** (Pointervergleich in C); Dateiname bleibt Persistenz-Key.

## 2) Kernentscheidungen

1. **Format:** `trk1` als eigenes kompaktes Binärformat (MUS-inspiriert).
2. **Streaming:** direkter Parser auf ByteArray-Cursor, kein Expandieren auf Heap.
3. **Ownership:** `RETAIN + owned by audio` (keine Kopie im MVP).
4. **API-Schnitt:** Clojure lädt/steuert Tracks über Native Calls, C verwaltet Playback.
5. **Threading:** keine VM-/RC-Aufrufe im Tick-Kontext.
6. **SFX-Modell:** `audio-play-sfx!` startet einen einmaligen Effekt (kein Loop), der Musik ueberlagern darf.
7. **Format-Policy:** **kein separates SFX-Format**; Musik und SFX verwenden beide `trk1`.
8. **Storage:** FS-Emulation als Persistenz; Composer mappt `track-sym <-> filename`.

## 3) Architektur

### Rollen

- **Clojure-Seite**
  - Authoring: Song als DSL/Map.
  - Compile: DSL/Map -> `trk1` ByteArray.
  - Persist: `trk1` per FS-Emulation als Datei speichern/laden.
  - Control: load/play/stop/unload, finished-Events pollen.
- **C-Seite**
  - Track-Registry (`track_id` -> retained byte-array view).
  - Stream-Parser + Voice-Scheduler im 1ms Tick.
  - One-Shot-SFX-Instanzen mit Priority/Steal-Regeln.
  - LEDC-Ausgabe auf konfigurierten Kanaelen.
  - Finished-Queue fuer Track-Ende.

### Datenfluss

1. `compile-track` in Clojure erzeugt `trk1` ByteArray.
2. Composer speichert Datei, z.B. `audio/menu-theme.trk1`.
3. Beim Laden: `slurp-bytes` liest Datei.
4. `audio-load-track!` validiert + `RETAIN` (track-id = interniertes Symbol/Keyword).
5. `audio-play-music!` startet Stream-Cursor auf Track.
6. Tick parst faellige Events und aktualisiert Voices/LEDC.
7. Bei `END` (+ Repeat verbraucht) -> `track_id` in finished queue.
8. `audio-play-sfx!` startet One-Shot-SFX parallel zur Musik.
9. Game-Loop holt via `audio-take-finished!` und startet Folge-Part.

## 4) `trk1` Spezifikation (bytegenau)

### Header (little-endian)

```text
offset size  field
0      4     magic = "TRK1"
4      1     version = 1
5      1     flags (bit0=default_loop, bit1=running_status_reserved)
6      1     channel_count (1..16)
7      1     reserved = 0
8      2     tpq (ticks per quarter)
10     2     bpm
12     4     stream_len_bytes
16     4     crc32_optional (0 = unused)
20     ...   event stream
```

### Event-Control-Byte

```text
bit7     has_delay_after
bit6..4  event_type (0=NOTE, 1=SET_VOL, 2=END, 3..7 reserved)
bit3..0  channel (0..15)
```

### Payloads

- `NOTE`
  - `note` u8 (`0..127`, `0`=Rest)
  - `gate_ticks` varuint
- `SET_VOL`
  - `volume` u8 (`0..255`)
- `END`
  - kein Payload

### Delay-Codierung

- Wenn `has_delay_after=1`: nach Payload folgt `delta_ticks` als Base-128 Varuint.
- Wenn `has_delay_after=0`: naechstes Event startet im selben Tick.

### Semantik

- Mehrere Events im selben Tick erlaubt.
- `END` beendet Stream; Loop/Repeat aus Play-Param oder Header-Flag.
- Gilt fuer Musik **und** SFX; SFX werden nur ueber API als One-Shot getriggert.

## 5) API-Vertrag

### Composer-Workflow (Clojure, explizit)

Im MVP wird die Persistenz ueber FS-Emulation abgewickelt; Runtime-ID und Dateiname sind getrennt.
Es gibt **kein separates SFX-Format**: SFX-Dateien sind ebenfalls `trk1`.

- `composer/compile-track [song]` -> `trk1-byte-array`
- `composer/save-track! [filename song]`:
  - kompiliert Song und speichert per `tiny-clj.fs/spit-bytes`
- `composer/load-track! [track-sym filename]`:
  - liest via `tiny-clj.fs/slurp-bytes`
  - ruft `audio-load-track!` mit `track-id = track-sym` auf
- `composer/register-track! [track-sym filename]` (optional Helper):
  - kapselt das Mapping in einer Clojure-Registry

Namenskonvention (MVP):

- Dateien unter `audio/*.trk1`
- `track-sym` als Runtime-ID, z.B. `:menu-theme`
- Dateiname als Persistenz-Ort, z.B. `\"audio/menu-theme.trk1\"`

### Clojure Native API

- `audio-load-track! [track-id trk1-bytes]` -> `true|false`
- `audio-unload-track! [track-id]` -> `true|false`
- `audio-play-music! [track-id repeat]` -> `true|false`
  - `repeat`: `1|:1x`, `2|:2x`, `0|:infinite|:unendlich`
- `audio-stop-track! [track-id]` -> `true|false` (stoppt den aktiven Stream dieses Tracks sofort, Track bleibt geladen)
  - Semantik: erzeugt **kein** `audio-take-finished!` Event fuer diesen Abbruch.
- `audio-stop-music! []` -> `nil`
- `audio-play-sfx! [sfx-id]` -> `true|false`
  - Semantik: startet einen **einmaligen** SFX-Run (One-Shot), auch wenn Musik laeuft.
  - Rueckgabe `false`, wenn SFX wegen voller Voice-Kapazitaet/Policy gedroppt wurde.
- `audio-stop-all! []` -> `nil`
- `audio-set-track-volume! [track-id vol-0-255]` -> `true|false`
  - Semantik: wenn Track aktiv laeuft, wirkt die Lautstaerke **sofort** (ohne Stop/Restart).
  - Wenn Track geladen aber inaktiv: Volume als Startwert fuer naechstes `audio-play-music!`.
- `audio-set-music-volume! [0-255]` -> `nil`
- `audio-take-finished! []` -> `track-id|nil`

Hinweis: `track-id` ist als **interniertes Symbol/Keyword** gedacht (Pointervergleich im C-Core).

### Interne C-Struktur (MVP)

- Track-Registry pro Track:
  - `track_id` (interniertes Symbol/Keyword; Pointervergleich)
  - `source_filename` (optional, nur Debug/Reload)
  - `retained_obj` (ID auf ByteArray)
  - `data_ptr`, `len`
- Pro aktiver Stream:
  - `cursor`, `stream_end`
  - `current_tick`, `next_event_tick`
  - `repeat_remaining`
  - `track_volume_8` (0..255, zur Laufzeit aenderbar)
  - `done`
- Pro Voice:
  - `freq_hz`, `gate_remaining_ticks`, `duty_8`, `mode`
- Pro aktive SFX-Instanz:
  - `sfx_id`, `cursor/phase`, `remain_ticks`, `priority`, `done`

## 6) Ownership- und Threading-Vertrag

### Ownership (`RETAIN + owned by audio`)

1. `audio-load-track!` validiert Header/Bounds und macht **genau ein** `RETAIN`.
2. `audio-unload-track!`/Shutdown macht **genau ein** `RELEASE`.
3. Tick greift nur read-only auf `data_ptr/len` zu.
4. Geladene Tracks gelten als **frozen** (kein `aset` auf Inhalt).

### Threading

- RC/Registry-Aenderungen nur im Control-Thread.
- Im Tick: kein `RETAIN`, kein `RELEASE`, keine VM-Aufrufe.
- Im Tick: **kein FS-I/O** (`slurp-bytes`/`spit-bytes` nur ausserhalb des Ticks).
- Registry-Update per kurzer kritischer Sektion oder atomarem Pointer-Swap.
- SFX-Start/Stop laeuft ueber Command-Queue; keine direkte ISR/VM-Kopplung.

## 7) Tick-Verhalten (1ms)

1. Bounded Command-Queue drainen (`PLAY_TRACK`, `STOP_TRACK`, `PLAY_SFX_ONESHOT`, `SET_TRACK_VOL`, `STOP_MUSIC`, `STOP_ALL`, `SET_VOL`, ...).
2. Fuer aktive Streams: wenn `current_tick >= next_event_tick`, Events parsen.
3. One-Shot-SFX schedulen (free voice bevorzugen, sonst Policy: steal oder drop).
4. Voice-State anpassen (`note`, `gate`, `volume`).
5. `gate_remaining_ticks` herunterzaehlen; bei 0 Note aus.
6. LEDC nur bei Frequenz-/Duty-Aenderung updaten.
7. Bei Track-Ende `track_id` in finished queue legen.
8. SFX-Ende setzt Voice in vorherigen Musikzustand zurueck (oder laesst Music-Stream normal weiterlaufen).

## 8) LEDC Mehrkanal-Setup

- Kanalanzahl aus Konfiguration (`audio_output_count`), nicht hardcoded.
- Board-Default aktuell:
  - `VG_PIN_PIEZO_1` -> Channel 0 / Timer 0
  - `VG_PIN_PIEZO_2` -> Channel 1 / Timer 1
- Validierung beim Init:
  - `audio_output_count <= hw_cap`
  - Channel/Timer Mapping konfliktfrei

## 9) Umsetzungsschritte (TDD)

### Schritt 0: Baseline

- `./build/unit-tests --test 'test_gpio_write*'`
- `./build/unit-tests --test 'test_runtime_stats*'`

DoD:

- Gruene Basis dokumentiert.

### Schritt 1: Native API Wiring

- Symbol-/Lookup-/Arity-Tests:
  - `test_audio_native_lookup_*`
  - `test_audio_native_arity_*`
  - `test_audio_load_unload_contract_*`
  - `test_audio_stop_track_contract_*`
  - `test_audio_set_track_volume_contract_*`
  - `test_audio_play_sfx_oneshot_contract_*`
  - `test_audio_track_id_symbol_contract_*`

DoD:

- Alle Audio-Natives aus `user` aufloesbar/callable.

### Schritt 2: `trk1` Parser + Streaming-Engine (Host)

- Tests fuer:
  - Header-Validierung (magic/version/bounds/channel_count)
  - Varint-Decoder inkl. Overflow-Schutz
  - Direkt-Streaming (kein Full-Decode)
  - Repeat-Verhalten (1x/2x/infinite)
  - Track-Stop-Verhalten (`audio-stop-track!` stoppt sofort, ohne unload)
  - Track-Volume-Verhalten (`audio-set-track-volume!` wirkt waehrend Playback ohne Neustart)
  - SFX-One-Shot-Verhalten (einmalig, kein Loop, parallel zu Musik)
  - Voice-Policy bei Engpass (free->steal->drop gemaess Konfiguration)
  - Finished-Meldung (`audio-take-finished!`)
  - RETAIN-Vertrag (`load->retain`, `unload->release`)
  - Composer-FS-Workflow (`compile -> spit-bytes -> slurp-bytes -> load`)
  - Symbol-ID + Mapping (`track_id = symbol`, `symbol -> filename`)

DoD:

- Deterministisches Verhalten in Host-Tests.

### Schritt 3: LEDC Bring-up (ESP32)

- Init fuer `audio_output_count` Ausgaenge.
- Smoke:
  - `N=1`: ein stabiler Ton
  - `N=2`: zwei unterschiedliche Frequenzen gleichzeitig
  - `N>2` (falls konfiguriert): alle Ausgaenge ansprechbar

DoD:

- Keine ungewollte Frequenzkopplung.

### Schritt 4: Scheduler + Telemetrie

- `esp_timer` 1ms start/stop.
- Counter:
  - `audio_cmd_drop_count`
  - `audio_tick_overrun_count`
  - `audio_queue_high_watermark`

DoD:

- Counter per Runtime-Stats/Debug sichtbar.

### Schritt 5: SFX + Handheld-Integration

- SFX-Programme (Laser/Explosion/Hit/Menu/R2D2).
- Game-Loop nutzt finished-Events fuer Folge-Parts.
- SFX werden waehrend laufender Musik als One-Shots getriggert (`audio-play-sfx!`).

DoD:

- Kontinuierliche Musik + SFX ohne Frame-Hitches.

### Schritt 6: Soak

- 30 min Board-Lauf.
- Jitter/Drops/Overruns auswerten.

DoD:

- Keine Abstuerze, keine progressive Verschlechterung.

## 10) Risiken und Gegenmassnahmen

- **Mutable Trackdaten (`aset`) nach `load**`
  - Mitigation: frozen contract + optionale Debug-Fingerprint-Pruefung.
- `**unload` waehrend Track noch aktiv**
  - Mitigation: mark-for-unload oder `stop` vor `release`.
- **Harte Lautstaerke-Spruenge waehrend Playback**
  - Mitigation: optional kleine Volume-Rampe (z.B. 2-8 Ticks) statt sofortigem Sprung.
- **SFX-Sturm bei engem Voice-Budget**
  - Mitigation: klare One-Shot-Policy (Prioritaet/Steal/Drop) + Drop-Counter.
- **Defekte/inkompatible `.trk1`-Datei in FS-Emulation**
  - Mitigation: strikte Header/Bounds-Validierung bei `audio-load-track!`, bei Fehler sauber ablehnen.
- **Track-Symbol zeigt auf falschen Dateinamen (Mapping-Fehler)**
  - Mitigation: zentrale Composer-Registry + Validierungscheck beim Laden.
- **Pointer-ID nach Runtime-Reset nicht stabil**
  - Mitigation: IDs nicht als Pointer persistieren; nach Boot/Reset ueber Symbolname neu registrieren.
- **Flash-Verschleiss durch haeufiges Re-Speichern**
  - Mitigation: nur bei inhaltlicher Aenderung speichern (Hash-Check), nicht bei jedem Boot.
- **Kanalanzahl > LEDC-Limits**
  - Mitigation: harte Init-Validierung + klarer Fehler.
- **Tick wird zu schwer**
  - Mitigation: bounded work pro Tick, keine Logs/Allokationen im Tick.
- **API-Drift Host vs ESP32**
  - Mitigation: gemeinsame Contract-Tests auf Host.

## 11) Anschluss an Hauptplan

Diese Datei konkretisiert Milestone 3/4 aus:
`/Users/theisen/Projects/Work/tiny-clj-feature/.cursor/plans/esp32_vector_handheld_geometri_dash.plan.md`

- Milestone 3 (Audio Core): Schritte 1 bis 4.
- Milestone 4 (Host Contract): Schritte 1, 5.


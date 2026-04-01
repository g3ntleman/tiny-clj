# Plan: Release-Sauberkeit und naechste Verbesserungen

**Stand: 01.04.2026**

## Ziel

Die aktuell betrachteten FS-, Sound- und Scene-Graph-Themen in zwei Bahnen ordnen:

1. Release-Sauberkeit: oeffentliche API, Tests, Build-/Test-Gates und Plandokumente auf einen konsistenten, auslieferbaren Stand bringen.
2. Naechste Verbesserungen: verbleibende Feature-, Memory- und CPU-Hebel nach Nutzen priorisieren.

## Audit-Fakten

- `tiny-clj.fs/read-block`, `write-block` und `set-size!` sind als oeffentliche API vorhanden.
- `tiny-clj.fs/delete!` ist aus `libs/tiny-clj/fs.clj` entfernt, die native Legacy-Bindung war aber noch im Lookup registriert.
- `tiny-clj.fs/spit-bytes` loescht bei `nil` bereits ueber die oeffentliche API.
- `tiny-clj.fs/meta-set!` und gemergte Listing-Metadaten sind jetzt release-fertig:
  - `meta-set!` speichert ueber einen nativen FS-Metadatenpfad in einen internen `0x01`-Sidecar statt ueber `tiny-db.kv/put-bytes`.
  - `fs_list_dir_batch(...)` liefert weiterhin formale Metadaten (`:size`, `:chunks`); der oeffentliche Wrapper `tiny-clj.fs/list` merged user-Metadaten aus dem Sidecar darueber.
  - Unlesbare oder nicht-mapbare Sidecar-Inhalte werden im Listing jetzt defensiv ignoriert statt den Listing-Pfad zu sprengen.
- Die Sidecar-Infrastruktur ist damit nicht nur vorbereitet, sondern fuer den oeffentlichen Metadatenpfad in Benutzung: der FS-Layer reserviert `0x01` als internes Metadaten-Suffix, filtert solche Keys nativ aus Listings und loescht sie inkl. Blob-Chunks beim Datei-Delete.
- Das ESP32-Sound-Backend nutzt inzwischen den per-tick-Loop im Timer-Callback; die Restarbeit dort ist Release-Hygiene, kein Architekturwechsel.
- M7b Dirty-Rect-/Entity-Level-Dirty-Tracking ist groesstenteils im Code, aber selektives per-rect Redraw statischer Inhalte bleibt offen.

## Release-Sauberkeit

### Sofort umsetzen

- Direkte Release-Regressionen fuer `spit-bytes nil`, `read-block`, `write-block`, `set-size!` und negative Randfaelle im C-Gate ergaenzen.
- Legacy-native Registrierung von `tiny-clj.fs/delete!` entfernen, damit Lookup und Doku wieder zusammenpassen.
- ESP32-Sound-Backend aufraeumen:
  - Kommentare auf den aktuellen one-shot/`VG_SOUND_TICK_MS`-Pfad bringen.
  - Neustart-/Shutdown-Zustand explizit zuruecksetzen.
  - Source-Contract absichern: kein `sound_engine_advance_ticks(...)` mehr im ESP32-Backend.
- Vollbuild + Volltests als Gate erneut laufen lassen.

### Ergebnis dieser Runde

- Erledigt:
  - neue C-Gate-Regressionen fuer `spit-bytes nil`, `read-block`, `write-block`, `set-size!` und negative Randfaelle
  - Legacy-native `tiny-clj.fs/delete!`-Registrierung entfernt
  - ESP32-Sound-Backend minimal bereinigt und per Source-Contract abgesichert
- Verifiziert:
  - `build-tests`
  - `./build/unit-tests --test 'test_file_io/*' --quiet` -> `45 Tests, 0 Failures`
  - `./build/unit-tests --test 'test_gpio_architecture_contract/*' --quiet` -> `18 Tests, 0 Failures`
  - `./build/unit-tests --test 'test_sound_engine/*' --quiet` -> `111 Tests, 0 Failures`
  - `./build/unit-tests` -> `2034 Tests, 0 Failures, 11 Ignored`
- Eingeordnet:
  - Die dabei sichtbaren Parse-/OOM-/`run-next-task`-Meldungen sind aktuell erwartete Ausgaben aus Negativtests bzw. Debugpfaden und kein offener Release-Blocker.

### Im Plan-/Doku-Stand nachziehen

- `Plans/fs-block-patch.md`: direkte Regressionen und aktueller Whole-file-Status dokumentieren.
- `Plans/fs-list-batch-metadata.md`: echte Implementierung von Wunschbild trennen.
- `Plans/esp32-rtos-sound-thread.plan.md`: Gate-Status und Cleanup-Abschluss nachziehen.

## Naechste Verbesserungen nach Nutzen

### Hoher CPU-/Bandwidth-Nutzen

1. Vector Scene Graph: selektives per-rect Erase/Redraw in `scene.c` statt vollem Slot-Redraw trotz Dirty-Rect-Erkennung.
2. FS Block Patch: echte chunk-orientierte `write-block`-/`read-block`-Pfade statt Whole-file-Materialisierung.
3. ESP32 Vector Scene Backend: Host-optimierte Dirty-Rect-/Transfer-Pfade auf echtes Device-Transferfenster/DMA heben.

### Hoher Feature-Nutzen

1. FS Metadata Follow-up: optional Diagnostik fuer ignorierte Sidecar-Fehler und nativen Merge nur bei echtem Embedded-Bedarf nachziehen.
2. Spatial Trigger / M10: generalisierten `:proximity`-/`SpatialRule`-Vertrag finalisieren.
3. Dirty-Rect-Debug-Overlay: `show_dirty_rects` fuer Host/ESP32-Debugging nachziehen.

### Mittlere Release-/Pflege-Relevanz

1. `set-size!`-Semantik entscheiden und dokumentieren: oeffentliche API behalten oder wieder hinter Metadaten-Workflow verstecken.
2. Embedded-Messungen fuer FS-Speicher- und Laufzeitverhalten erfassen.
3. User Guide / usage docs fuer Vector Scene Graph aktualisieren.

## Reihenfolge

1. Release-Sauberkeit abschliessen.
2. FS-Metadaten-Feature auf Edge-Cases und Embedded-Kosten absichern.
3. Danach Performance-Arbeit: per-rect redraw, blockorientiertes FS patching, ESP32 transfer path.

## Erfolgskriterien

- Oeffentliche FS-API ist konsistent mit Lookup, Doku und Tests.
- Sound-Backend-Plan ist mit gruenen Gates abgeschlossen.
- Das Vollsuite-Release-Gate ist gruen.
- Offene Plaene unterscheiden sauber zwischen implementiert, teilweise implementiert und Wunscharchitektur.
- Die naechsten Verbesserungen sind priorisiert statt nur gesammelt.
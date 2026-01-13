## ESP32 Integration: FlashDB Partition / FAL backend (Scaffold)

Diese Repo-Variante von `flashdb-reloaded` unterstützt:

- **Host**: `FILE_MODE` (POSIX) – für schnelle Tests auf macOS/Linux.
- **ESP32**: `FAL_MODE` – über eine minimale FAL-Implementierung, die auf der ESP‑IDF `esp_partition_*` API basiert.

### Partition-Label

FlashDB erwartet als `path` bei `fdb_kvdb_init` / `fdb_tsdb_init` im FAL‑Modus den **Partition‑Label** (String), z. B. `"flashdb"`.

### Beispiel Partition Table (CSV)

```csv
# Name,   Type, SubType, Offset,  Size
flashdb, data,  0x40,    0x200000, 0x100000
```

Wichtig:
- **Erase-Granularität** ist i. d. R. 4KB (`esp_partition_erase_range`).
- `sec_size` sollte ein Vielfaches der Erase‑Granularität sein (typisch 4096).

### Build-Notes

- `external/flashdb-reloaded/inc/fdb_cfg.h` schaltet auf ESP32 automatisch von `FILE_MODE` auf `FAL_MODE` um (`ESP32_BUILD`).
- Die minimalen FAL-Symbole liegen in `external/flashdb-reloaded/port/fal/`.

### Nächster Schritt (real target wiring)

Für eine echte ESP‑IDF Integration muss `ESP32_BUILD` gesetzt werden und das Projekt muss in eine ESP‑IDF Build-Struktur eingebunden werden (inkl. ESP‑IDF Includes und Linker-Script/Partition table).


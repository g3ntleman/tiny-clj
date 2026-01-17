# tiny-db

`tiny-db` is an embedded-friendly C storage library used by `tiny-clj`.

It provides:

- A **sorted key/value database** backed by a flash-friendly 4.4BSD B-Tree variant.
- A **large-value (“blob”) layer** implemented on top of the KV DB (chunked; no overflow pages).
- A practical pattern for **streaming file-system emulation** (implemented in `tiny-clj` on top of `tiny-db`).

This project is designed for **crash safety (power-loss)** and **small, deterministic RAM usage** on embedded targets (e.g. ESP32).

## Features (high level)

- **Binary keys and values**: APIs use `(pointer, length)` and do not depend on NUL-terminated strings.
- **Sorted KV store**: point lookups plus efficient prefix iteration (cursor API).
- **Flash-friendly write pattern**: append-only log records aligned to erase blocks (no read-modify-write).
- **Crash/power-loss safety**: recovery is based on validating and replaying log records; latest valid record wins.
- **Large values (streaming)**: store and read large blobs without holding the full value in RAM.
- **Incremental GC/compaction**: reclaim space without requiring full scans on every operation.
- **Tunable RAM cache**: small page cache by default; can be increased (and auto-sized on ESP32 with PSRAM).

## Database types

### 1) KV database (B-Tree)

The primary database is a sorted **key/value store**:

- **Keys**: arbitrary bytes (`void* + length`) — no C-string/NUL terminator requirement.
- **Values**: stored inline if they fit on a page; otherwise handled by the blob layer (below).
- **Queries**: point lookup and prefix iteration.

Internally this uses:

- A 4.4BSD B-Tree implementation (adapted for embedded constraints).
- A custom **log-structured mpool**: each B-Tree page is stored as an **append-only record** on the underlying block device.

Crash safety model:

- Writes are append-only; the latest valid record wins.
- Recovery scans the log and rebuilds necessary state.

### 2) Blob database (large values over KV)

Large values are stored via a blob layer on top of the KV DB. This avoids classic B-Tree overflow pages and keeps on-flash writes aligned to the erase block size.

Key ideas:

- Values are split into **fixed-size chunks** (typically `erase_granularity - header_size`, e.g. `4096 - 16 = 4080` bytes).
- Chunk metadata and chunk lists are stored as regular KV entries.
- Read/write APIs support **streaming**, so RAM usage stays bounded.

Public APIs:

- `tdb_blob_*` functions (see `inc/tiny_db.h`), including `tdb_blob_stream()` and `tdb_blob_writer_*()`.

Note: `tiny-db` does **not** include a separate time-series DB (TSDB). Earlier TSDB code was removed because `tiny-clj` uses an RRD-based approach instead (`external/tiny-db/src/tdb_kv.c` contains the note).

## Streaming file-system emulation (used by tiny-clj)

`tiny-db` itself is a KV + blob database. The “file system” behavior is built in `tiny-clj` on top of it (see `src/fs_layer.c` in the main repo).

The pattern:

1. A **file metadata key** holds small metadata (e.g. EDN map fields like `:version`, `:chunks`, `:size`).
2. File contents are stored as **chunk keys** derived from `(path, version, chunk_index)`.
3. Reads iterate chunks and invoke a **stream sink callback** with bounded chunk sizes (the application never needs to allocate the whole file in RAM).

This provides:

- **Streaming read/write** of large files with O(1) RAM.
- Simple crash-safety semantics: new content becomes visible after metadata commit.

## Why this is a good fit for embedded systems

`tiny-db` is intended for microcontrollers and embedded Linux-class devices where RAM is scarce and power loss is normal.

- **Bounded RAM**: streaming APIs and small caches keep peak RAM roughly constant with file/value size.
- **Deterministic behavior**: no background threads; work is explicit (e.g. incremental GC steps).
- **Wear-aware**: the log-structured design favors sequential/append writes aligned to erase blocks.
- **Crash safety without heavyweight dependencies**: recovery uses simple record scanning and CRC checks.
- **Pluggable storage**: the `tdb_blockdev_t` interface lets you target NOR flash, SPI flash, SD-backed images, RAM test devices, etc.

## Block device model

`tiny-db` is storage-backend agnostic. You provide a `tdb_blockdev_t` with:

- `read(addr, len)`
- `prog(addr, len)` (NOR-flash 1→0 semantics)
- `erase(addr, len)`

and geometry:

- `read_granularity`
- `prog_granularity`
- `erase_granularity` (also the record size for B-Tree pages)

See `src/tdb_blockdev.h` and `inc/tiny_db.h`.

## RAM usage and cache tuning

### O(1)-RAM mapping

`tiny-db` runs with `TDB_MPOOL_O1_RAM` enabled: it avoids an O(partition-size) page-offset table by using a small fixed cache + scan-on-demand for mapping misses.

This is essential for large partitions on embedded systems.

### Page cache size

The mpool uses a small RAM page cache. It is intentionally configurable:

- Default is optimized for low RAM usage.
- If PSRAM is available (ESP32), the cache can auto-size higher for better throughput.

APIs:

- `tdb_mpool_set_cache_pagecount(uint32_t count)`
- `tdb_mpool_get_cache_pagecount(void)`
- `tdb_mpool_enable_psram_autosize(int enable)`

## Quick start (C)

```c
#include "tiny_db.h"

// 1) Provide a tdb_blockdev_t (read/prog/erase + geometry).
// 2) Open KV database.
tdb_kv_t* kv = NULL;
tdb_kv_cfg_t cfg = { .start_page = TDB_KV_ROOT_PAGE };

tdb_status_t st = tdb_kv_open(&kv, &my_bdev, &cfg);
if (st != TDB_OK) { /* handle */ }

// Put/get with binary keys.
const uint8_t key[] = {0x01, 0x02, 0x00, 0x03};
const uint8_t val[] = {0xAA, 0xBB};
tdb_kv_put(kv, key, sizeof(key), val, sizeof(val));

tdb_blob_t out = {0};
tdb_kv_get(kv, key, sizeof(key), &out);

tdb_kv_close(kv);
```

## Build / integration (CMake)

In the parent project:

- `add_subdirectory(external/tiny-db)`
- `target_link_libraries(your-target PRIVATE tiny-db)`
- Include headers via `external/tiny-db/inc` (public) and `external/tiny-db/inc/tiny-db/tiny-db.h` (umbrella include).

## License and provenance

Parts of the B-Tree implementation originate from the 4.4BSD reference code, adapted for this project’s embedded/flash constraints. See `external/bsd-btree-4.4bsd/REFERENCE.md` in the main repo for provenance notes.


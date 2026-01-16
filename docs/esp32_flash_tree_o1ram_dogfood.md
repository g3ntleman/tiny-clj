## ESP32 dogfooding: flash-tree `FT_MPOOL_O1_RAM` (scan-on-demand mapping)

### Goal

Exercise the **O(1) RAM** mpool mode (`FT_MPOOL_O1_RAM=1`) on real ESP32 hardware under hard reset / power-loss conditions.

### Preconditions

- ESP32 flash erase sector size is typically **4 KiB**.
- Your build uses the ESP32 toolchain file `toolchains/esp32.cmake` (so `ESP32_BUILD` is propagated into `flash-tree` by `external/flash-tree/CMakeLists.txt`).

### Build (CMake)

Example (adapt toolchain paths as needed):

```bash
cmake -S . -B _build_esp32 -DCMAKE_TOOLCHAIN_FILE=toolchains/esp32.cmake -DCMAKE_BUILD_TYPE=Embedded
cmake --build _build_esp32 --target tiny-clj-esp32
```

### Runtime dogfood strategy (power-loss simulation)

Implement a small state machine that runs at boot and forces a reset at different points.

- **State storage**: store `phase` in one of:
  - RTC memory (survives reset, not power-loss), or
  - NVS (survives power-loss), or
  - a dedicated KV key inside flash-tree (survives power-loss; simplest for this project).

Suggested phases:

1. **Phase 0**: Create KV, write key `dogfood/a` = `1`, sync/close. Then force reset.
2. **Phase 1**: Reopen KV, verify `dogfood/a == 1`. Write `dogfood/a = 2`. Force reset *immediately* after write call (no explicit GC).
3. **Phase 2**: Reopen, verify `dogfood/a == 2`. Create many updates (e.g. 1000 puts) to grow the log. Force reset mid-loop.
4. **Phase 3**: Reopen, verify invariants:
   - reads always return either the last fully written value or the previous one (never corrupt/garbage)
   - DB remains openable; no CRC errors propagate as success
5. **Phase 4**: Optionally call `ft_kv_gc_step_more()` in a loop with a small budget, and force resets during GC to ensure recovery remains consistent.

Notes:
- For *true* power-loss, cut power while running Phase 2/4.
- For “hard reset” only, call `esp_restart()` after advancing the phase key.

### What to watch

- No OOM during mount: `page_offsets[]` must not be allocated in `FT_MPOOL_O1_RAM`.
- After power-loss, mount must succeed and key reads must not return corrupted bytes.
- GC (if invoked) must not brick the DB across resets (worst case: it does no work, but never corrupts committed state).


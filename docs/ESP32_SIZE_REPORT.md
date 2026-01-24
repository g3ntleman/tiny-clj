## ESP32 size report (Tiny-CLJ)

This document records a reproducible, toolchain-backed size snapshot for the ESP32 build **without feature stripping** (i.e., no intentional removal of capabilities; only portability fixes + build instrumentation).

### Build recipe

- **Toolchain**: repo-local Xtensa GCC from `./external/espressif-tools/`
- **ESP-IDF (submodule)**: `external/esp-idf` pinned to `v5.3.4`
- **Configure**:

```bash
ESP32_TOOLCHAIN_PATH="$PWD/external/espressif-tools/tools/xtensa-esp-elf/esp-13.2.0_20240530/xtensa-esp-elf" \
cmake -S . -B builds/esp32 -G "Unix Makefiles" \
  -DCMAKE_TOOLCHAIN_FILE="$PWD/toolchains/esp32.cmake" \
  -DCMAKE_BUILD_TYPE=Embedded \
  -DCMAKE_RUNTIME_OUTPUT_DIRECTORY="$PWD/builds/esp32" \
  -DTINYCLJ_EMBEDDED_STRIP_FEATURES=OFF \
  -DTINYCLJ_LINKER_MAP=ON

cmake --build builds/esp32 -j 8 --target tiny-clj-esp32
```

- **Outputs**:
  - `builds/esp32/tiny-clj-esp32` (ELF)
  - `builds/esp32/tiny-clj-esp32.map` (linker map)

### Section sizes (xtensa-esp32-elf-size)

From:

```bash
xtensa-esp32-elf-size builds/esp32/tiny-clj-esp32
```

- **text**: 191,451
- **data**: 4,268
- **bss**: 1,361
- **dec**: 197,080

Updated snapshot (after removing libc printf/vsnprintf pull-ins, still **without feature stripping**):

- **text**: 170,299
- **data**: 4,060
- **bss**: 1,348
- **dec**: 175,707

More detailed (`-A`):

- `.text`: 166,943
- `.rodata`: 24,392
- `.data`: 4,252
- `.bss`: 1,308

### Largest symbols (nm --size-sort)

Initial snapshot (before printf/vsnprintf pull-in removal) had:

- `_vfprintf_r` (~11,997)
- `_svfprintf_r` (~11,801)

Current biggest individual symbols include:

- `_vfiprintf_r` (~7,938)
- `_svfiprintf_r` (~7,782)
- `_strtod_l` (~33,331)
- `parse_expr` (~3,729)
- `eval_list` (~3,515)
- `canonicalize_expr_with_scope` (~3,307)

### Mapfile highlights

Previously, the linker map showed `printf`/`vsnprintf` pulling in newlib’s large formatting core.

Current build removes the `printf`/`vfprintf`/`snprintf`/`vsnprintf` pull-ins by routing internal formatting/logging through a small formatter (`mini_snprintf`/`clj_mini_vsnprintf`) and non-printf output paths.

### Notes / next optimization targets (without feature stripping)

- **Compiler/Linker flags** are already size-focused (`-Os`, sections, GC). Remaining wins typically come from:
  - reducing printf-family usage in non-essential paths (often the biggest libc pull-in)
  - optimizing Tiny-CLJ hot functions: `parse_expr`, `eval_list`, `canonicalize_expr_with_scope`
  - tuning third-party deps (e.g., tiny-db configuration) if they are over-provisioned for the target.


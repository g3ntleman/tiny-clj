#!/usr/bin/env bash
#
# Report ESP32 Release code size for flash-tree vs flashdb.
#
# This builds split archives:
#   - libflashdb-core.a     (FlashDB implementation: src/flashdb/*.c)
#   - libflash-tree-core.a  (flash-tree glue/core: src/ft_*.c excluding src/flashdb)
#   - libbsd-btree.a        (BSD btree/mpool implementation)
#
# And prints section totals and ratios using xtensa-esp32-elf-size.
#
# Usage:
#   external/flash-tree/scripts/size_report_esp32.sh
#
# Overrides:
#   BUILD_DIR=external/flash-tree/build-esp32-size
#   TOOLCHAIN_FILE=toolchains/esp32.cmake
#   SIZE_TOOL=xtensa-esp32-elf-size
#   AR_TOOL=xtensa-esp32-elf-ar
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
FT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
REPO_ROOT="$(cd "${FT_DIR}/../.." && pwd)"

BUILD_DIR="${BUILD_DIR:-${FT_DIR}/build-esp32-size}"
TOOLCHAIN_FILE="${TOOLCHAIN_FILE:-${REPO_ROOT}/toolchains/esp32.cmake}"

SIZE_TOOL="${SIZE_TOOL:-xtensa-esp32-elf-size}"
AR_TOOL="${AR_TOOL:-xtensa-esp32-elf-ar}"

# Fallback: if ESP32_TOOLCHAIN_PATH is set, derive tool paths from it.
if ! command -v "${SIZE_TOOL}" >/dev/null 2>&1; then
  if [ -n "${ESP32_TOOLCHAIN_PATH:-}" ] && [ -x "${ESP32_TOOLCHAIN_PATH}/bin/xtensa-esp32-elf-size" ]; then
    SIZE_TOOL="${ESP32_TOOLCHAIN_PATH}/bin/xtensa-esp32-elf-size"
  fi
fi

if ! command -v "${AR_TOOL}" >/dev/null 2>&1; then
  if [ -n "${ESP32_TOOLCHAIN_PATH:-}" ] && [ -x "${ESP32_TOOLCHAIN_PATH}/bin/xtensa-esp32-elf-ar" ]; then
    AR_TOOL="${ESP32_TOOLCHAIN_PATH}/bin/xtensa-esp32-elf-ar"
  else
    # Fall back to host ar (often works fine for .a inspection)
    AR_TOOL="ar"
  fi
fi

if ! command -v "${SIZE_TOOL}" >/dev/null 2>&1; then
  echo "ERROR: SIZE_TOOL not found: ${SIZE_TOOL}"
  echo "Tip: source ./scripts/esp_env.sh (ESP-IDF) or set ESP32_TOOLCHAIN_PATH."
  exit 1
fi

if [ ! -f "${TOOLCHAIN_FILE}" ]; then
  echo "ERROR: TOOLCHAIN_FILE not found: ${TOOLCHAIN_FILE}"
  echo "Set TOOLCHAIN_FILE=... to your esp32 CMake toolchain file."
  exit 1
fi

cmake -S "${FT_DIR}" -B "${BUILD_DIR}" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE="${TOOLCHAIN_FILE}"

cmake --build "${BUILD_DIR}" -j --target flashdb-core flash-tree-core bsd-btree

FLASHDB_A="${BUILD_DIR}/libflashdb-core.a"
FLASH_TREE_A="${BUILD_DIR}/libflash-tree-core.a"
BSD_A="${BUILD_DIR}/libbsd-btree.a"

for a in "${FLASHDB_A}" "${FLASH_TREE_A}" "${BSD_A}"; do
  if [ ! -f "${a}" ]; then
    echo "ERROR: missing archive: ${a}"
    exit 1
  fi
done

tmpdir="$(mktemp -d)"
cleanup() { rm -rf "${tmpdir}"; }
trap cleanup EXIT

sum_sections_for_archive() {
  local archive="$1"
  local label="$2"
  local out_dir="${tmpdir}/${label}"
  mkdir -p "${out_dir}"

  # Extract members (xtensa size is more reliable on .o than on .a across toolchains).
  (cd "${out_dir}" && "${AR_TOOL}" x "${archive}")

  # Sum selected sections.
  # Xtensa often uses: .text, .literal, .rodata, .data, .bss
  # We report FLASH = .text+.literal+.rodata+.data, RAM(.bss) separately.
  local report
  report="$("${SIZE_TOOL}" -A "${out_dir}"/*.o 2>/dev/null || true)"
  if [ -z "${report}" ]; then
    echo "ERROR: size tool produced no output for ${label}"
    exit 1
  fi

  # Parse: each line is "<section> <size> <addr>"
  # Some tools print headers; ignore non-numeric sizes.
  local flash bss
  flash="$(printf '%s\n' "${report}" | awk '\n    $1 ~ /^\\.(text|literal|rodata|data)$/ && $2 ~ /^[0-9]+$/ { s += $2 }\n    END { printf \"%d\", s+0 }\n  ')"
  bss="$(printf '%s\n' "${report}" | awk '\n    $1 == \".bss\" && $2 ~ /^[0-9]+$/ { s += $2 }\n    END { printf \"%d\", s+0 }\n  ')"

  printf "%-16s flash=%9d  bss=%9d  (%s)\\n" "${label}" "${flash}" "${bss}" "$(basename "${archive}")"
  printf "%d %d\n" "${flash}" "${bss}"
}

read -r flashdb_flash flashdb_bss < <(sum_sections_for_archive "${FLASHDB_A}" "flashdb-core")
read -r ftcore_flash ftcore_bss     < <(sum_sections_for_archive "${FLASH_TREE_A}" "flash-tree-core")
read -r bsd_flash bsd_bss           < <(sum_sections_for_archive "${BSD_A}" "bsd-btree")

flash_tree_total_flash=$((ftcore_flash + bsd_flash))
flash_tree_total_bss=$((ftcore_bss + bsd_bss))

echo ""
printf "%-16s flash=%9d  bss=%9d\\n" "flash-tree-total" "${flash_tree_total_flash}" "${flash_tree_total_bss}"

echo ""
python3 - <<PY
flashdb = ${flashdb_flash}
ft = ${flash_tree_total_flash}
ratio = (ft / flashdb) if flashdb else float('inf')
print(f"ratio(flash-only): {ratio:.3f}  (flash-tree-total / flashdb-core)")
PY


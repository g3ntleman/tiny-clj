#!/usr/bin/env bash
#
# Report ESP32 Release code size for tiny-db.
#
# This builds split archives:
#   - libtiny-db-core.a     (tiny-db glue/core: src/tdb_*.c)
#   - libbsd-btree.a        (BSD btree/mpool implementation)
#
# And prints section totals and ratios using xtensa-esp32-elf-size.
#
# Usage:
#   external/tiny-db/scripts/size_report_esp32.sh
#
# Overrides:
#   BUILD_DIR=builds/esp32
#   TOOLCHAIN_FILE=toolchains/esp32.cmake
#   SIZE_TOOL=xtensa-esp32-elf-size
#   AR_TOOL=xtensa-esp32-elf-ar
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TDB_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
REPO_ROOT="$(cd "${TDB_DIR}/../.." && pwd)"

BUILD_DIR="${BUILD_DIR:-${REPO_ROOT}/builds/esp32}"
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

cmake -S "${TDB_DIR}" -B "${BUILD_DIR}" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE="${TOOLCHAIN_FILE}"

cmake --build "${BUILD_DIR}" -j --target tiny-db-core bsd-btree

TINY_DB_A="${BUILD_DIR}/libtiny-db-core.a"
BSD_A="${BUILD_DIR}/libbsd-btree.a"

for a in "${TINY_DB_A}" "${BSD_A}"; do
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

read -r tdbcore_flash tdbcore_bss   < <(sum_sections_for_archive "${TINY_DB_A}" "tiny-db-core")
read -r bsd_flash bsd_bss           < <(sum_sections_for_archive "${BSD_A}" "bsd-btree")

tiny_db_total_flash=$((tdbcore_flash + bsd_flash))
tiny_db_total_bss=$((tdbcore_bss + bsd_bss))

echo ""
printf "%-16s flash=%9d  bss=%9d\\n" "tiny-db-total" "${tiny_db_total_flash}" "${tiny_db_total_bss}"


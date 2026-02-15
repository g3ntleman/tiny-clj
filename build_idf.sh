
#!/usr/bin/env bash
#
# Build tiny-clj ESP-IDF app (esp32-idf/).
#
# Usage:
#   ./build_idf.sh [--clean] [--target esp32|esp32s3|...] [--flash] [--monitor] [-- -DIDF_CMAKE_OPT=...]
#
# Notes:
# - This script is intentionally non-interactive and safe to run multiple times.
# - Use --clean when you changed EXCLUDE_COMPONENTS or sdkconfig.defaults and want a fresh configure.
set -euo pipefail

usage() {
  cat <<'EOF'
Usage:
  ./build_idf.sh [options] [-- <extra idf.py args>]

Options:
  --clean         Remove esp32-idf/build and esp32-idf/sdkconfig before building.
  --target <t>    Set ESP-IDF target (default: esp32). Example: esp32s3
  --flash         Run `idf.py flash` after a successful build.
  --monitor       Run `idf.py monitor` after a successful build (implies --flash).
  --no-move       Do not move build to builds/esp32-idf (keeps esp32-idf/build for incremental builds).
  -h, --help      Show this help.

Examples:
  ./build_idf.sh --clean
  ./build_idf.sh --target esp32s3 --clean
  ./build_idf.sh --flash
  ./build_idf.sh --monitor
  ./build_idf.sh --no-move --monitor   # Keep build for incremental; use "flash+monitor (no build)" to re-test same binary
  ./build_idf.sh --clean -- --verbose
EOF
}

TARGET="esp32"
DO_CLEAN=0
DO_FLASH=0
DO_MONITOR=0
DO_MOVE=1
declare -a EXTRA_IDF_ARGS=()

while [ $# -gt 0 ]; do
  case "$1" in
    --clean)
      DO_CLEAN=1
      shift
      ;;
    --target)
      if [ $# -lt 2 ]; then
        echo "ERROR: --target requires a value" >&2
        exit 2
      fi
      TARGET="$2"
      shift 2
      ;;
    --flash)
      DO_FLASH=1
      shift
      ;;
    --monitor)
      DO_MONITOR=1
      DO_FLASH=1
      shift
      ;;
    --no-move)
      DO_MOVE=0
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    --)
      shift
      while [ $# -gt 0 ]; do
        EXTRA_IDF_ARGS+=("$1")
        shift
      done
      ;;
    *)
      echo "ERROR: Unknown argument: $1" >&2
      echo >&2
      usage >&2
      exit 2
      ;;
  esac
done

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}" && pwd)"
IDF_PROJECT_DIR="${REPO_ROOT}/esp32-idf"
CENTRAL_BUILD_DIR="${REPO_ROOT}/builds/esp32-idf"

if [ ! -d "${IDF_PROJECT_DIR}" ]; then
  echo "ERROR: ESP-IDF project dir not found: ${IDF_PROJECT_DIR}" >&2
  exit 1
fi

# Activate ESP-IDF environment for this repo.
# shellcheck disable=SC1091
source "${REPO_ROOT}/scripts/esp_env.sh"

cd "${IDF_PROJECT_DIR}"

# Repair stale/dangling symlinks from interrupted runs.
if [ -L build ] && [ ! -e build ]; then
  rm -f build
fi
if [ -L "${CENTRAL_BUILD_DIR}/build" ] && [ ! -e "${CENTRAL_BUILD_DIR}/build" ]; then
  rm -f "${CENTRAL_BUILD_DIR}/build"
fi

if [ "${DO_CLEAN}" -eq 1 ]; then
  rm -rf build
  rm -rf "${CENTRAL_BUILD_DIR}/build"
  rm -f sdkconfig
fi

# With --no-move: reuse central build if present so next run is incremental.
if [ "${DO_MOVE}" -eq 0 ] && [ ! -d build ] && [ -d "${CENTRAL_BUILD_DIR}/build" ]; then
  mv "${CENTRAL_BUILD_DIR}/build" build
  echo "Restored build from ${CENTRAL_BUILD_DIR}/build for incremental builds."
fi

# set-target only when no build (first run or after --clean); else incremental.
if [ ! -d build ]; then
  idf.py set-target "${TARGET}"
fi

run_idf_build_logged() {
  local log_file="$1"
  set +e
  # Nounset-safe array expansion (EXTRA_IDF_ARGS may be unset/non-array in some shells).
  idf.py build ${EXTRA_IDF_ARGS[@]+"${EXTRA_IDF_ARGS[@]}"} 2>&1 | tee "${log_file}"
  local rc=${PIPESTATUS[0]}
  set -e
  return "${rc}"
}

BUILD_LOG="$(mktemp -t tinyclj-idf-build.XXXXXX.log)"
if ! run_idf_build_logged "${BUILD_LOG}"; then
  if grep -q "Cannot find component list file" "${BUILD_LOG}"; then
    echo "Detected stale CMake component metadata. Reconfiguring from a clean build directory..."
    rm -rf build
    idf.py set-target "${TARGET}"
    run_idf_build_logged "${BUILD_LOG}"
  else
    echo "Build failed. Log: ${BUILD_LOG}" >&2
    exit 1
  fi
fi
rm -f "${BUILD_LOG}"

# If requested, run flash/monitor before moving the build dir.
if [ "${DO_FLASH}" -eq 1 ]; then
  idf.py flash ${EXTRA_IDF_ARGS[@]+"${EXTRA_IDF_ARGS[@]}"}
fi

if [ "${DO_MONITOR}" -eq 1 ]; then
  idf.py monitor ${EXTRA_IDF_ARGS[@]+"${EXTRA_IDF_ARGS[@]}"}
fi

# Optionally move the produced build directory into the centralized builds area.
if [ -d build ] && [ "${DO_MOVE}" -eq 1 ] && [ ! -L build ]; then
  mkdir -p "${CENTRAL_BUILD_DIR}"
  if [ -d "${CENTRAL_BUILD_DIR}/build" ]; then
    rm -rf "${CENTRAL_BUILD_DIR}/build"
  fi
  mv build "${CENTRAL_BUILD_DIR}/"
  ln -s "${CENTRAL_BUILD_DIR}/build" build
  echo "Moved esp32-idf/build -> ${CENTRAL_BUILD_DIR}/build"
  echo "Linked esp32-idf/build -> ${CENTRAL_BUILD_DIR}/build (for monitor/addr2line)."
fi

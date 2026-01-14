#!/usr/bin/env bash
#
# Source this to activate ESP-IDF (submodule) environment for this repo.
#
# Usage:
#   source ./scripts/esp_env.sh
#
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

export IDF_PATH="${IDF_PATH:-${REPO_ROOT}/external/esp-idf}"
export IDF_TOOLS_PATH="${IDF_TOOLS_PATH:-${REPO_ROOT}/_deps/espressif-tools}"

if [ ! -d "${IDF_PATH}" ]; then
  echo "ERROR: IDF_PATH not found: ${IDF_PATH}" >&2
  echo "Run: git submodule update --init --recursive external/esp-idf" >&2
  return 1 2>/dev/null || exit 1
fi

if [ ! -f "${IDF_PATH}/export.sh" ]; then
  echo "ERROR: ESP-IDF export script not found: ${IDF_PATH}/export.sh" >&2
  return 1 2>/dev/null || exit 1
fi

mkdir -p "${IDF_TOOLS_PATH}"

# shellcheck disable=SC1091
source "${IDF_PATH}/export.sh"

echo "✅ ESP-IDF environment active"
echo "   IDF_PATH=${IDF_PATH}"
echo "   IDF_TOOLS_PATH=${IDF_TOOLS_PATH}"

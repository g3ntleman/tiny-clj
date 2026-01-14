#!/usr/bin/env bash
#
# Bootstrap ESP-IDF (as a git submodule) + toolchain for this repo.
#
# This keeps tools repo-local by default (under ./_deps/) so contributors
# don't need to install anything globally.
#
# Usage:
#   ./scripts/setup_esp_idf.sh
#
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
IDF_SUBMODULE_PATH="${REPO_ROOT}/external/esp-idf"

export IDF_TOOLS_PATH="${IDF_TOOLS_PATH:-${REPO_ROOT}/_deps/espressif-tools}"

echo "== ESP-IDF bootstrap =="
echo "Repo: ${REPO_ROOT}"
echo "IDF submodule: ${IDF_SUBMODULE_PATH}"
echo "IDF_TOOLS_PATH: ${IDF_TOOLS_PATH}"
echo ""

if [ ! -d "${IDF_SUBMODULE_PATH}" ]; then
  echo "Initializing ESP-IDF submodule..."
  git -C "${REPO_ROOT}" submodule update --init --recursive external/esp-idf
fi

if [ ! -f "${IDF_SUBMODULE_PATH}/install.sh" ]; then
  echo "ERROR: ESP-IDF install.sh not found at ${IDF_SUBMODULE_PATH}/install.sh" >&2
  echo "Try: git -C \"${REPO_ROOT}\" submodule update --init --recursive external/esp-idf" >&2
  exit 1
fi

mkdir -p "${IDF_TOOLS_PATH}"

echo "Running ESP-IDF install.sh (this downloads toolchains + python env)..."
echo "This may take a while on first run."
echo ""

(cd "${IDF_SUBMODULE_PATH}" && ./install.sh)

echo ""
echo "✅ ESP-IDF installed."
echo ""
echo "Next:"
echo "  source ./scripts/esp_env.sh"
echo "  which xtensa-esp32-elf-size"

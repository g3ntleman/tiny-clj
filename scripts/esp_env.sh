#!/usr/bin/env bash
#
# Source this to activate ESP-IDF (submodule) environment for this repo.
#
# Usage:
#   source ./scripts/esp_env.sh
#
# NOTE:
# - This file is meant to be sourced from interactive shells (bash/zsh).
# - Avoid 'set -u' here, because ESP-IDF's export.sh touches variables that may
#   be unset in some shells, which can cause noisy "parameter not set" errors.
set -eo pipefail

# Cross-shell (bash/zsh) repo root detection:
# - When sourced from zsh, BASH_SOURCE is not set.
# - Prefer git, fall back to current working directory heuristics.
REPO_ROOT="$(
  git rev-parse --show-toplevel 2>/dev/null || true
)"
if [ -z "${REPO_ROOT}" ]; then
  REPO_ROOT="$(pwd)"
fi

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

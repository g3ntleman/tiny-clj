#!/usr/bin/env bash
#
# Build (and optionally run) the tiny-db host tests in a subproject-local build dir.
#
# Default build dir:
#   external/tiny-db/build
#
# Usage:
#   scripts/tiny_db_tests.sh            # build
#   RUN=1 scripts/tiny_db_tests.sh      # build + run
#   CLEAN=1 scripts/tiny_db_tests.sh    # clean + build
#   BUILD_DIR=/tmp/ft RUN=1 scripts/tiny_db_tests.sh
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC_DIR="$ROOT_DIR/external/tiny-db"
BUILD_DIR="${BUILD_DIR:-$SRC_DIR/build}"

RUN="${RUN:-0}"
CLEAN="${CLEAN:-0}"
BUILD_TYPE="${BUILD_TYPE:-Debug}"

if [ "$CLEAN" = "1" ]; then
  rm -rf "$BUILD_DIR"
fi

cmake -S "$SRC_DIR" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE="$BUILD_TYPE"
cmake --build "$BUILD_DIR" -j --target tiny-db-tests

if [ "$RUN" = "1" ]; then
  "$BUILD_DIR/tiny-db-tests"
fi


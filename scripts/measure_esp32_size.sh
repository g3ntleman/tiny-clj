#!/usr/bin/env bash
set -euo pipefail

root_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

build_dir="${1:-}"
if [[ -z "$build_dir" ]]; then
  if [[ -d "$root_dir/build-release" ]]; then
    build_dir="$root_dir/build-release"
  else
    build_dir="$root_dir/build"
  fi
fi

# Allow passing a relative path like "build-release"
if [[ "$build_dir" != /* ]]; then
  build_dir="$root_dir/$build_dir"
fi

bin_path=""
if [[ -f "$build_dir/bin/tiny-clj-esp32" ]]; then
  bin_path="$build_dir/bin/tiny-clj-esp32"
elif [[ -f "$build_dir/tiny-clj-esp32" ]]; then
  bin_path="$build_dir/tiny-clj-esp32"
fi

if [[ -z "$bin_path" ]]; then
  echo "Building tiny-clj-esp32 in $build_dir" >&2
  make -C "$build_dir" tiny-clj-esp32

  if [[ -f "$build_dir/bin/tiny-clj-esp32" ]]; then
    bin_path="$build_dir/bin/tiny-clj-esp32"
  elif [[ -f "$build_dir/tiny-clj-esp32" ]]; then
    bin_path="$build_dir/tiny-clj-esp32"
  fi
fi

if [[ -z "$bin_path" ]]; then
  echo "ERROR: tiny-clj-esp32 not found after build in $build_dir" >&2
  exit 1
fi

file_size_bytes=""
if stat -f "%z" "$bin_path" >/dev/null 2>&1; then
  file_size_bytes="$(stat -f "%z" "$bin_path")"
elif stat -c "%s" "$bin_path" >/dev/null 2>&1; then
  file_size_bytes="$(stat -c "%s" "$bin_path")"
fi

if [[ -n "$file_size_bytes" ]]; then
  echo "$bin_path: $file_size_bytes bytes"
else
  echo "$bin_path"
fi

# Prefer Xtensa size tool for ESP32 ELFs if available.
xtensa_size=""
if command -v xtensa-esp32-elf-size >/dev/null 2>&1; then
  xtensa_size="$(command -v xtensa-esp32-elf-size)"
else
  # Repo-local toolchain (when scripts/esp_env.sh was used to populate external/)
  candidate="$(ls -1 "$root_dir"/external/espressif-tools/tools/xtensa-esp-elf/*/xtensa-esp-elf/bin/xtensa-esp32-elf-size 2>/dev/null | head -n 1 || true)"
  if [[ -n "$candidate" && -x "$candidate" ]]; then
    xtensa_size="$candidate"
  fi
fi

if [[ -n "$xtensa_size" ]]; then
  "$xtensa_size" "$bin_path"
  exit 0
fi

if size -m "$bin_path" >/dev/null 2>&1; then
  size -m "$bin_path" | awk '
    /^Segment __TEXT:/ ||
    /^Section __text:/ ||
    /^Section __cstring:/ ||
    /^Section __const:/ ||
    /^Segment __DATA:/ ||
    /^Section __data:/ ||
    /^Section __bss:/
  '
else
  size "$bin_path" || true
fi

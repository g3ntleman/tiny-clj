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

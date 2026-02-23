#!/usr/bin/env python3
"""
Generate a compact C header from a Hershey .jhf font subset.

Default source:
  https://raw.githubusercontent.com/kamalmostafa/hershey-fonts/master/hershey-fonts/futural.jhf

This script keeps only a small embedded-friendly character subset and emits
pre-normalized stroke segments (int8 coordinates) for direct use in C code.
"""

from __future__ import annotations

import argparse
import datetime as _dt
import pathlib
import sys
import urllib.request


DEFAULT_URL = (
    "https://raw.githubusercontent.com/kamalmostafa/hershey-fonts/"
    "master/hershey-fonts/futural.jhf"
)

# Keep this narrow and practical for low-res UI text.
DEFAULT_CHARSET = (
    " "
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "0123456789"
    ".,:-_/\\"
)


def _fetch_lines(source: str) -> list[str]:
    if source.startswith("http://") or source.startswith("https://"):
        with urllib.request.urlopen(source, timeout=30) as resp:
            data = resp.read().decode("ascii", "ignore")
            return data.splitlines()
    return pathlib.Path(source).read_text(encoding="ascii", errors="ignore").splitlines()


def _parse_jhf_line(line: str) -> tuple[int, int, list[tuple[int, int, int, int]]]:
    """
    Parse one Hershey JHF glyph line into:
      advance, width_units, list of line segments (x1,y1,x2,y2)

    Coordinates are normalized:
      x' = x - left
      y' = y + 12      # maps common top y=-12 to y'=0
    """
    if len(line) < 10:
        return 8, 8, []

    try:
        _ = int(line[0:5])  # glyph id (unused)
    except ValueError:
        return 8, 8, []

    left = ord(line[8]) - ord("R")
    right = ord(line[9]) - ord("R")
    width = max(1, right - left)
    advance = width + 2

    payload = line[10:]
    points: list[tuple[int, int] | None] = []
    for i in range(0, len(payload) - 1, 2):
        a = payload[i]
        b = payload[i + 1]
        if a == " " and b == "R":
            points.append(None)
            continue
        x = ord(a) - ord("R")
        y = ord(b) - ord("R")
        points.append((x - left, y + 12))

    segs: list[tuple[int, int, int, int]] = []
    prev: tuple[int, int] | None = None
    for p in points:
        if p is None:
            prev = None
            continue
        if prev is not None:
            segs.append((prev[0], prev[1], p[0], p[1]))
        prev = p

    return advance, width, segs


def _emit_header(
    out_path: pathlib.Path,
    source: str,
    charset: str,
    glyph_rows: list[tuple[int, int, list[tuple[int, int, int, int]]]],
) -> None:
    out_path.parent.mkdir(parents=True, exist_ok=True)
    now = _dt.datetime.now(_dt.timezone.utc).strftime("%Y-%m-%d %H:%M:%S UTC")

    segs_flat: list[tuple[int, int, int, int]] = []
    glyph_meta: list[tuple[int, int, int]] = []  # offset,count,advance
    for advance, _w, segs in glyph_rows:
        offset = len(segs_flat)
        segs_flat.extend(segs)
        glyph_meta.append((offset, len(segs), advance))

    guard = "TINY_CLJ_VTEXT_HERSHEY_SIMPLEX_SUBSET_H"
    lines: list[str] = []
    lines.append(f"#ifndef {guard}")
    lines.append(f"#define {guard}")
    lines.append("")
    lines.append("#include <stdint.h>")
    lines.append("")
    lines.append("/*")
    lines.append(" * Auto-generated Hershey subset for tiny-clj VText.")
    lines.append(f" * Source: {source}")
    lines.append(f" * Generated: {now}")
    lines.append(" */")
    lines.append("")
    lines.append("typedef struct {")
    lines.append("    int8_t x1, y1, x2, y2;")
    lines.append("} VgHersheySeg;")
    lines.append("")
    lines.append("typedef struct {")
    lines.append("    uint16_t seg_offset;")
    lines.append("    uint8_t seg_count;")
    lines.append("    uint8_t advance;")
    lines.append("} VgHersheyGlyph;")
    lines.append("")
    lines.append(f"#define VG_HERSHEY_SUBSET_COUNT {len(charset)}")
    lines.append("")
    c_escaped = charset.replace("\\", "\\\\").replace("\"", "\\\"")
    lines.append(f"static const char vg_hershey_subset_chars[{len(charset) + 1}] =")
    lines.append(f"    \"{c_escaped}\";")
    lines.append("")
    lines.append(
        f"static const VgHersheySeg vg_hershey_subset_segs[{max(1, len(segs_flat))}] = {{"
    )
    if not segs_flat:
        lines.append("    {0,0,0,0},")
    else:
        for x1, y1, x2, y2 in segs_flat:
            lines.append(f"    {{{x1},{y1},{x2},{y2}}},")
    lines.append("};")
    lines.append("")
    lines.append(
        f"static const VgHersheyGlyph vg_hershey_subset_glyphs[{len(glyph_meta)}] = {{"
    )
    for off, cnt, adv in glyph_meta:
        lines.append(f"    {{{off},{cnt},{adv}}},")
    lines.append("};")
    lines.append("")
    lines.append("#endif")
    lines.append("")

    out_path.write_text("\n".join(lines), encoding="utf-8")


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--source", default=DEFAULT_URL, help="Path or URL to .jhf font")
    ap.add_argument(
        "--charset",
        default=DEFAULT_CHARSET,
        help="Character subset to export (order preserved)",
    )
    ap.add_argument(
        "--output",
        default="src/vtext_hershey_simplex_subset.h",
        help="Output header path",
    )
    args = ap.parse_args(argv)

    lines = _fetch_lines(args.source)
    if len(lines) < 95:
        print("Input does not look like a valid Hershey ASCII font file.", file=sys.stderr)
        return 2

    rows: list[tuple[int, int, list[tuple[int, int, int, int]]]] = []
    for ch in args.charset:
        code = ord(ch)
        if code < 32 or code >= 32 + len(lines):
            rows.append((8, 8, []))
            continue
        rows.append(_parse_jhf_line(lines[code - 32]))

    _emit_header(pathlib.Path(args.output), args.source, args.charset, rows)
    print(f"Wrote {args.output} with {len(rows)} glyphs.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))

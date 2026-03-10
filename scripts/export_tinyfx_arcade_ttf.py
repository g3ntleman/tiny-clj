#!/usr/bin/env python3
"""
Export the tiny-fx arcade glyph set as an installable TrueType font.

The script reads the active glyph definitions from src/vector_scene_graph.c so
the generated font stays aligned with the renderer implementation.
"""

from __future__ import annotations

import argparse
import math
import pathlib
import re
from dataclasses import dataclass, field

from fontTools.fontBuilder import FontBuilder
from fontTools.pens.ttGlyphPen import TTGlyphPen


FULL_UNIT = 100
HALF_UNIT = FULL_UNIT // 2
STROKE_THICKNESS = 80
UNITS_PER_EM = 1200
ASCENT = 1000
DESCENT = 200
TOP_MARGIN = 100
FONT_FAMILY = "Tiny FX Arcade"
FONT_STYLE = "Regular"


@dataclass
class GlyphSpec:
    advance: int
    segments: list[tuple[int, int, int, int]] = field(default_factory=list)
    boxes: list[tuple[int, int, int, int]] = field(default_factory=list)


def _decode_c_char(token: str) -> str:
    if token == r"\\":
        return "\\"
    if token == r"\'":
        return "'"
    return token


def _glyph_name(ch: str) -> str:
    names = {
        " ": "space",
        ".": "period",
        ",": "comma",
        ":": "colon",
        ";": "semicolon",
        "!": "exclam",
        "?": "question",
        "%": "percent",
        "(": "parenleft",
        ")": "parenright",
        "-": "hyphen",
        "_": "underscore",
        "/": "slash",
        "\\": "backslash",
        "+": "plus",
    }
    if ch in names:
        return names[ch]
    if "A" <= ch <= "Z":
        return ch
    if "0" <= ch <= "9":
        return f"digit_{ch}"
    return f"uni{ord(ch):04X}"


def _is_alnum_arcade(ch: str) -> bool:
    return ("A" <= ch <= "Z") or ("0" <= ch <= "9")


def _full_to_font_y(y_half: int) -> int:
    # tiny-fx glyph coordinates use screen space (y grows downward). TrueType
    # uses a classic font coordinate system (y grows upward), so we must invert
    # the renderer y-axis during export.
    return (ASCENT - TOP_MARGIN) - (y_half * HALF_UNIT)


def _segment_polygon(
    x1_half: int, y1_half: int, x2_half: int, y2_half: int
) -> list[tuple[int, int]]:
    x1 = x1_half * HALF_UNIT
    y1 = _full_to_font_y(y1_half)
    x2 = x2_half * HALF_UNIT
    y2 = _full_to_font_y(y2_half)
    dx = x2 - x1
    dy = y2 - y1
    length = math.hypot(dx, dy)
    if length == 0:
        return []

    half_thickness = STROKE_THICKNESS / 2.0
    nx = (-dy / length) * half_thickness
    ny = (dx / length) * half_thickness
    tx = (dx / length) * half_thickness
    ty = (dy / length) * half_thickness

    return [
        (int(round(x1 - tx + nx)), int(round(y1 - ty + ny))),
        (int(round(x1 - tx - nx)), int(round(y1 - ty - ny))),
        (int(round(x2 + tx - nx)), int(round(y2 + ty - ny))),
        (int(round(x2 + tx + nx)), int(round(y2 + ty + ny))),
    ]


def _box_polygon(x0_half: int, y0_half: int, x1_half: int, y1_half: int) -> list[tuple[int, int]]:
    left = min(x0_half, x1_half) * HALF_UNIT
    right = max(x0_half, x1_half) * HALF_UNIT
    top = _full_to_font_y(min(y0_half, y1_half))
    bottom = _full_to_font_y(max(y0_half, y1_half))
    return [
        (left, top),
        (left, bottom),
        (right, bottom),
        (right, top),
    ]


def _draw_polygon(
    pen: TTGlyphPen, polygon: list[tuple[int, int]], points: list[tuple[int, int]]
) -> None:
    if len(polygon) < 3:
        return
    pen.moveTo(polygon[0])
    for point in polygon[1:]:
        pen.lineTo(point)
    pen.closePath()
    points.extend(polygon)


def _build_notdef() -> tuple[object, tuple[int, int]]:
    pen = TTGlyphPen(None)
    points: list[tuple[int, int]] = []
    _draw_polygon(
        pen,
        [(80, 120), (80, 920), (720, 920), (720, 120)],
        points,
    )
    _draw_polygon(
        pen,
        [(180, 220), (620, 220), (620, 820), (180, 820)],
        points,
    )
    return pen.glyph(), (800, 80)


def _build_glyph(spec: GlyphSpec) -> tuple[object, tuple[int, int]]:
    pen = TTGlyphPen(None)
    points: list[tuple[int, int]] = []

    for x1, y1, x2, y2 in spec.segments:
        _draw_polygon(pen, _segment_polygon(x1, y1, x2, y2), points)

    for x0, y0, x1, y1 in spec.boxes:
        _draw_polygon(pen, _box_polygon(x0, y0, x1, y1), points)

    if not points:
        return pen.glyph(), (spec.advance * FULL_UNIT, 0)

    x_min = min(x for x, _ in points)
    return pen.glyph(), (spec.advance * FULL_UNIT, x_min)


def _extract_case_block(source: str) -> str:
    match = re.search(r"switch \(c\)\s*\{(?P<body>.*?)(?:^\s*default:)", source, re.S | re.M)
    if not match:
        raise ValueError("Could not locate glyph switch in src/vector_scene_graph.c")
    return match.group("body")


def _parse_vector_scene_graph(source_path: pathlib.Path) -> dict[str, GlyphSpec]:
    body = _extract_case_block(source_path.read_text(encoding="utf-8"))
    result: dict[str, GlyphSpec] = {}

    case_matches = list(
        re.finditer(r"^\s*case\s+'((?:\\\\|\\'|[^']))':(?P<body>.*?)(?=^\s*case\s+'|^\s*default:)", body, re.S | re.M)
    )
    if not case_matches:
        raise ValueError("Could not parse any glyph cases from src/vector_scene_graph.c")

    for match in case_matches:
        ch = _decode_c_char(match.group(1))
        case_body = match.group("body")
        spec = GlyphSpec(advance=10 if _is_alnum_arcade(ch) else 8)
        base_x_half = 2 if _is_alnum_arcade(ch) else 0

        adv_match = re.search(r"adv\s*=\s*(\d+)", case_body)
        if adv_match:
            spec.advance = int(adv_match.group(1))

        for macro_match in re.finditer(r"(GLH?|GLCELLBOX)\(([^)]*)\)", case_body):
            macro = macro_match.group(1)
            args = [int(part.strip()) for part in macro_match.group(2).split(",")]
            if macro == "GL":
                x1, y1, x2, y2 = args
                spec.segments.append(
                    (base_x_half + (x1 * 2), y1 * 2, base_x_half + (x2 * 2), y2 * 2)
                )
            elif macro == "GLH":
                x1h, y1h, x2h, y2h = args
                spec.segments.append((base_x_half + x1h, y1h, base_x_half + x2h, y2h))
            elif macro == "GLCELLBOX":
                x, y = args
                spec.boxes.append(
                    (base_x_half + (x * 2), y * 2, base_x_half + ((x + 1) * 2), (y + 1) * 2)
                )

        result[ch] = spec

    return result


def _add_synthetic_glyphs(glyphs: dict[str, GlyphSpec]) -> None:
    glyphs["+"] = GlyphSpec(
        advance=10,
        segments=[
            (2 + (4 * 2), 0, 2 + (4 * 2), 16),
            (2 + 0, 8, 2 + 8 * 2, 8),
        ],
    )


def _build_font(
    glyph_specs: dict[str, GlyphSpec], output_path: pathlib.Path, version: str
) -> None:
    glyph_order = [".notdef"]
    glyphs = {}
    metrics = {}
    cmap = {}
    glyph_name_for_char: dict[str, str] = {}

    notdef_glyph, notdef_metrics = _build_notdef()
    glyphs[".notdef"] = notdef_glyph
    metrics[".notdef"] = notdef_metrics

    for ch in sorted(glyph_specs.keys(), key=lambda value: ord(value)):
        name = _glyph_name(ch)
        if name in glyphs:
            continue
        glyph, metric = _build_glyph(glyph_specs[ch])
        glyph_order.append(name)
        glyphs[name] = glyph
        metrics[name] = metric
        glyph_name_for_char[ch] = name

    for ch in "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 .,;:!?%()-_/\\+":
        if ch in glyph_name_for_char:
            cmap[ord(ch)] = glyph_name_for_char[ch]

    for upper in "ABCDEFGHIJKLMNOPQRSTUVWXYZ":
        lower = upper.lower()
        if upper in glyph_name_for_char:
            cmap[ord(lower)] = glyph_name_for_char[upper]

    fb = FontBuilder(UNITS_PER_EM, isTTF=True)
    fb.setupGlyphOrder(glyph_order)
    fb.setupCharacterMap(cmap)
    fb.setupGlyf(glyphs)
    fb.setupHorizontalMetrics(metrics)
    fb.setupHorizontalHeader(ascent=ASCENT, descent=-DESCENT)
    fb.setupNameTable(
        {
            "familyName": FONT_FAMILY,
            "styleName": FONT_STYLE,
            "fullName": f"{FONT_FAMILY} {FONT_STYLE}",
            "psName": "TinyFXArcade-Regular",
            "version": f"Version {version}",
            "uniqueFontIdentifier": f"{FONT_FAMILY} {FONT_STYLE}; tiny-clj",
        }
    )
    fb.setupOS2(
        sTypoAscender=ASCENT,
        sTypoDescender=-DESCENT,
        usWinAscent=ASCENT,
        usWinDescent=DESCENT,
    )
    fb.setupPost()
    fb.setupMaxp()
    output_path.parent.mkdir(parents=True, exist_ok=True)
    fb.save(str(output_path))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--source",
        default="/Users/theisen/Projects/tiny-clj/src/vector_scene_graph.c",
        help="Path to src/vector_scene_graph.c",
    )
    parser.add_argument(
        "--output",
        default="/Users/theisen/Projects/tiny-clj/resources/TinyFXArcade.ttf",
        help="Output .ttf path",
    )
    parser.add_argument(
        "--version",
        default="1.000",
        help="Version string written into the font metadata",
    )
    args = parser.parse_args()

    source_path = pathlib.Path(args.source)
    output_path = pathlib.Path(args.output)
    glyph_specs = _parse_vector_scene_graph(source_path)
    _add_synthetic_glyphs(glyph_specs)
    _build_font(glyph_specs, output_path, args.version)
    print(f"Wrote {output_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

#!/usr/bin/env python3
"""Emit a C brace-initialized byte array body for #include from a binary file."""
import argparse
import sys


def main() -> int:
    p = argparse.ArgumentParser()
    p.add_argument("input", help="Binary input path")
    p.add_argument("output", help="C fragment output path (.inc)")
    args = p.parse_args()
    with open(args.input, "rb") as f:
        data = f.read()
    lines = []
    row = []
    for i, b in enumerate(data):
        row.append(f"0x{b:02x}")
        if len(row) >= 12:
            lines.append("  " + ", ".join(row) + ",")
            row = []
    if row:
        lines.append("  " + ", ".join(row))
    with open(args.output, "w", encoding="ascii") as out:
        out.write("\n".join(lines))
        out.write("\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3

import argparse
import pathlib
import re

MAX_RAW_STRING_DELIMITER_LEN = 16


def sanitize_symbol(symbol: str) -> str:
    sanitized = re.sub(r"[^A-Za-z0-9_]", "_", symbol).strip("_")
    if not sanitized:
        sanitized = "EMBEDDED_CLOJURE"
    if sanitized[0].isdigit():
        sanitized = f"_{sanitized}"
    return sanitized.upper()


def trim_base(base: str, suffix: str = "") -> str:
    max_len = MAX_RAW_STRING_DELIMITER_LEN - len(suffix)
    if max_len <= 0:
        raise ValueError("raw-string suffix exceeds delimiter length limit")
    trimmed = base[:max_len]
    if not trimmed:
        trimmed = "EMBEDDED"[:max_len]
    return trimmed


def choose_delimiter(payload: bytes, symbol: str) -> bytes:
    base = sanitize_symbol(symbol)
    candidate = trim_base(base).encode("ascii")
    suffix = 0
    while b")" + candidate + b"\"" in payload:
        suffix += 1
        suffix_text = f"_{suffix}"
        candidate = (trim_base(base, suffix_text) + suffix_text).encode("ascii")
    return candidate


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Generate a C++ raw-string include snippet from a Clojure source file."
    )
    parser.add_argument("--input", required=True, help="Path to the Clojure source file.")
    parser.add_argument("--output", required=True, help="Path to the generated include snippet.")
    parser.add_argument(
        "--symbol",
        required=True,
        help="Stable symbol/input name used to derive a deterministic raw-string delimiter.",
    )
    args = parser.parse_args()

    input_path = pathlib.Path(args.input)
    output_path = pathlib.Path(args.output)

    payload = input_path.read_bytes()
    delimiter = choose_delimiter(payload, args.symbol)

    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_bytes(b'R"' + delimiter + b"(" + payload + b")" + delimiter + b'"\n')
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

#!/usr/bin/env python3
"""ESP32 REPL smoke test with heap-aware checkpoints."""

import re
import time
import serial

PORT = "/dev/cu.usbserial-0001"
BAUD = 115200

FORM_RE = re.compile(r"form=(\d+)")
RE_HEAP_UNSUPPORTED = re.compile(r"Cannot call heap as a function|Unable to resolve symbol: heap")


def sanitize(text: str) -> str:
    # Drop non-printable bytes from occasional UART garbage.
    return "".join(ch for ch in text if ch in "\r\n\t" or 32 <= ord(ch) < 127)


def read_lines(ser: serial.Serial, seconds: float):
    deadline = time.time() + seconds
    lines = []
    while time.time() < deadline:
        raw = ser.readline()
        if not raw:
            continue
        line = sanitize(raw.decode("utf-8", errors="replace")).strip()
        if not line:
            continue
        print(line)
        lines.append(line)
    return lines


def send_cmd(ser: serial.Serial, cmd: str):
    print(f">>> {cmd}")
    ser.write(cmd.encode("utf-8") + b"\r\n")

def hard_reset_board(ser: serial.Serial):
    # Typical ESP32 auto-reset wiring: RTS->EN, DTR->IO0.
    # Keep IO0 high and pulse EN low->high.
    ser.dtr = False
    ser.rts = True
    time.sleep(0.12)
    ser.rts = False
    time.sleep(0.9)

def heap_supported(ser: serial.Serial) -> bool:
    # (heap expr) exists only in DEBUG builds.
    send_cmd(ser, "(heap nil)")
    lines = read_lines(ser, 3.0)
    return not any(RE_HEAP_UNSUPPORTED.search(line) for line in lines)

def heap_checkpoint(ser: serial.Serial, label: str):
    print(f"--- {label} ---")
    send_cmd(ser, "(heap nil)")
    return read_lines(ser, 4.0)


def main():
    ser = serial.Serial(PORT, BAUD, timeout=0.2)
    print(f"Connected to {PORT} at {BAUD}")

    # Hard reset so each smoke run starts from a clean VM/heap state.
    hard_reset_board(ser)
    time.sleep(0.8)

    # Settle + clear pending noise
    ser.reset_input_buffer()
    ser.write(b"\x03")
    time.sleep(0.4)
    ser.reset_input_buffer()

    heap_mode = "special-form" if heap_supported(ser) else "disabled"
    print(f"Heap mode: {heap_mode}")
    if heap_mode == "special-form":
        heap_checkpoint(ser, "Heap before require")

    print("--- Require tiny-fx.sound-demos ---")
    if heap_mode == "special-form":
        send_cmd(ser, "(heap (require 'tiny-fx.sound-demos))")
    else:
        send_cmd(ser, "(require 'tiny-fx.sound-demos)")

    require_lines = read_lines(ser, 120.0)
    max_form = 0
    hard_error = False
    for line in require_lines:
        m = FORM_RE.search(line)
        if m:
            max_form = max(max_form, int(m.group(1)))
        if (
            "OutOfMemoryError" in line
            or "UNHANDLED:" in line
            or "assert failed" in line
            or "abort() was called" in line
        ):
            hard_error = True

    heap_after_lines = []
    if heap_mode == "special-form" and not hard_error:
        heap_after_lines = heap_checkpoint(ser, "Heap after require")

    expr_ok = False
    if not hard_error:
        print("--- Follow-up expression ---")
        send_cmd(ser, "(+ 1 2)")
        expr_lines = read_lines(ser, 4.0)
        expr_ok = any(line == "3" or line.endswith(" 3") for line in expr_lines)
    else:
        print("--- Follow-up expression skipped (require failed) ---")

    print("--- Summary ---")
    print(f"heap_mode={heap_mode}")
    print(f"max_form={max_form}")
    print(f"require_hard_error={hard_error}")
    print(f"expr_ok={expr_ok}")
    print(f"heap_after_samples={len(heap_after_lines)}")

    ser.close()


if __name__ == "__main__":
    main()

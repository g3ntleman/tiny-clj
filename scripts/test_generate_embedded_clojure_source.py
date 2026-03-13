#!/usr/bin/env python3

import pathlib
import subprocess
import sys
import tempfile
import unittest


class GenerateEmbeddedClojureSourceTests(unittest.TestCase):
    def run_generator(self, source_bytes: bytes, symbol: str = "tiny_fx_sound.clj.inc") -> bytes:
        script_path = pathlib.Path(__file__).with_name("generate_embedded_clojure_source.py")
        with tempfile.TemporaryDirectory() as tmp_dir:
            tmp_path = pathlib.Path(tmp_dir)
            input_path = tmp_path / "input.clj"
            output_path = tmp_path / "output.inc"
            input_path.write_bytes(source_bytes)
            subprocess.run(
                [
                    sys.executable,
                    str(script_path),
                    "--input",
                    str(input_path),
                    "--output",
                    str(output_path),
                    "--symbol",
                    symbol,
                ],
                check=True,
            )
            return output_path.read_bytes()

    @staticmethod
    def unwrap_payload(output: bytes) -> tuple[bytes, bytes]:
        if not output.startswith(b'R"'):
            raise AssertionError("generated output must start with a raw string literal")
        delimiter, remainder = output[2:].split(b"(", 1)
        suffix = b")" + delimiter + b'"\n'
        if not remainder.endswith(suffix):
            raise AssertionError("generated output must end with the matching raw string delimiter")
        payload = remainder[: -len(suffix)]
        return delimiter, payload

    def test_preserves_payload_bytes_exactly(self) -> None:
        source = b"(ns demo)\n(def note \"A#4\")\n"
        generated = self.run_generator(source)
        delimiter, payload = self.unwrap_payload(generated)

        self.assertTrue(delimiter)
        self.assertEqual(source, payload)

    def test_avoids_raw_string_delimiter_collisions(self) -> None:
        source = b"(ns demo)\n;; )COLLIDE\" appears inside the payload\n"
        generated = self.run_generator(source, symbol="collide")
        delimiter, payload = self.unwrap_payload(generated)

        self.assertEqual(source, payload)
        self.assertEqual(b"COLLIDE_1", delimiter)


if __name__ == "__main__":
    unittest.main()

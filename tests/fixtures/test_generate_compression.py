import argparse
import gzip
import hashlib
import json
import subprocess
import sys
import unittest
from pathlib import Path
from tempfile import TemporaryDirectory

import bpy


GENERATOR = Path(__file__).with_name("generate_compression.py")


class CompressionFixtureTests(unittest.TestCase):
    reader = None

    def run_generator(self, directory, *options, success=True):
        result = subprocess.run(
            [
                bpy.app.binary_path, "--background", "--factory-startup",
                "--disable-autoexec", "--python-exit-code", "1",
                "--python", str(GENERATOR), "--", "--output", str(directory),
                "--reader", str(self.reader), "--profile", "small", *options,
            ],
            capture_output=True, text=True, timeout=120,
        )
        output = result.stdout + result.stderr
        if success:
            self.assertEqual(result.returncode, 0, output)
        else:
            self.assertNotEqual(result.returncode, 0, output)
        return output

    def test_small_cases_and_evidence(self):
        with TemporaryDirectory(prefix="blend compression test-") as directory:
            root = Path(directory)
            self.run_generator(root)
            evidence = json.loads((root / "manifest.json").read_text(encoding="ascii"))
            self.assertEqual(evidence["blender_version_tuple"], list(bpy.app.version))
            self.assertEqual(evidence["profile"], "small")
            self.assertEqual(
                [case["case"] for case in evidence["cases"]],
                ["large_mesh", "repetitive", "packed_assets"],
            )
            for case in evidence["cases"]:
                raw = (root / case["raw"]["path"]).read_bytes()
                self.assertEqual(hashlib.sha256(raw).hexdigest(), case["raw"]["sha256"])
                for record in case["compressed"]:
                    saved = (root / record["path"]).read_bytes()
                    self.assertEqual(len(saved), record["stored_bytes"])
                    self.assertEqual(hashlib.sha256(saved).hexdigest(), record["sha256"])
                    decoded = record["decoded_bytes"]
                    ratio = (decoded + len(saved) - 1) // len(saved)
                    self.assertEqual(ratio, record["minimum_integer_ratio"])
                    if record["codec"] == "gzip":
                        self.assertEqual(gzip.decompress(saved), raw)
                        self.assertEqual(record["minimum_window_log"], 10)
                        self.assertFalse(record["blender_written_compression"])
                    else:
                        self.assertEqual(saved[:4], b"\x28\xb5\x2f\xfd")
                        self.assertTrue(record["blender_written_compression"])

    def test_existing_output_is_not_overwritten(self):
        with TemporaryDirectory(prefix="blend-compression-existing-") as directory:
            root = Path(directory)
            self.run_generator(root, "--case", "repetitive")
            saved = {path.name: path.read_bytes() for path in root.iterdir()}
            self.assertIn(
                "Measurement output already exists",
                self.run_generator(root, "--case", "repetitive", success=False),
            )
            self.assertEqual(saved, {path.name: path.read_bytes() for path in root.iterdir()})

    def test_missing_reader_is_explicit(self):
        with TemporaryDirectory(prefix="blend-compression-reader-") as directory:
            root = Path(directory)
            self.assertIn(
                "Reader executable is missing",
                self.run_generator(
                    root / "output", "--reader", str(root / "missing.exe"), success=False,
                ),
            )
            self.assertFalse((root / "output").exists())

    def test_duplicate_cases_are_rejected(self):
        with TemporaryDirectory(prefix="blend-compression-duplicate-") as directory:
            root = Path(directory) / "output"
            self.assertIn(
                "Each --case may be supplied only once",
                self.run_generator(
                    root, "--case", "repetitive", "--case", "repetitive", success=False,
                ),
            )
            self.assertFalse(root.exists())


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--reader", required=True, type=Path)
    arguments = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    CompressionFixtureTests.reader = parser.parse_args(arguments).reader.resolve()
    unittest.main(argv=[__file__])

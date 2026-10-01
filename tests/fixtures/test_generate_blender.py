import subprocess
import unittest
from pathlib import Path
from tempfile import TemporaryDirectory

import bpy


GENERATOR = Path(__file__).with_name("generate_blender.py")


class BlenderFixtureTests(unittest.TestCase):
    def run_generator(self, *arguments, success=True):
        result = subprocess.run(
            [
                bpy.app.binary_path,
                "--background", "--factory-startup", "--disable-autoexec",
                "--python-exit-code", "1", "--python", str(GENERATOR),
                "--", *map(str, arguments),
            ],
            capture_output=True,
            text=True,
            timeout=120,
        )
        output = result.stdout + result.stderr
        if success:
            self.assertEqual(result.returncode, 0, output)
        else:
            self.assertNotEqual(result.returncode, 0, output)
        return output

    def test_reproducible_and_non_destructive_checks(self):
        with TemporaryDirectory(prefix="blend-fixture-test-") as directory:
            first = Path(directory) / "first.blend"
            second = Path(directory) / "second.blend"
            self.run_generator("--output", first)
            self.run_generator("--output", second)
            expected = first.read_bytes()
            self.assertEqual(expected, second.read_bytes())

            for mode in ("--check", "--check-bytes"):
                self.run_generator("--output", first, mode)
                self.assertEqual(expected, first.read_bytes())

            modified = expected[:-1] + bytes([expected[-1] ^ 1])
            first.write_bytes(modified)
            output = self.run_generator(
                "--output", first, "--check-bytes", success=False
            )
            self.assertIn("Fixture bytes differ", output)
            self.assertEqual(modified, first.read_bytes())

    def test_content_check_reads_stored_scene(self):
        with TemporaryDirectory(prefix="blend-scene-test-") as directory:
            fixture = Path(directory) / "scaled.blend"
            bpy.ops.wm.read_factory_settings(use_empty=True)
            bpy.context.scene.unit_settings.scale_length = 7.0
            bpy.data.libraries.write(
                str(fixture), {bpy.context.scene}, fake_user=True, compress=False
            )
            expected = fixture.read_bytes()
            output = self.run_generator("--output", fixture, "--check", success=False)
            self.assertIn("Empty fixture must use a unit scale of 1", output)
            self.assertEqual(expected, fixture.read_bytes())

    def test_content_check_rejects_synthetic_header(self):
        fixture = (
            GENERATOR.resolve().parents[2]
            / "plugins/usdBlendFileFormat/tests/fixtures/header_only.blend"
        )
        expected = fixture.read_bytes()
        output = self.run_generator("--output", fixture, "--check", success=False)
        self.assertIn("uncompressed Blender 5.2 format-1 file", output)
        self.assertEqual(expected, fixture.read_bytes())


if __name__ == "__main__":
    unittest.main(argv=[__file__])
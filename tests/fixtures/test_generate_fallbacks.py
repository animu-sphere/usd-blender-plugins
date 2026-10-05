import subprocess
import unittest
from pathlib import Path
from tempfile import TemporaryDirectory

import bpy


GENERATOR = Path(__file__).with_name("generate_fallbacks.py")


class FallbackFixtureTests(unittest.TestCase):
    def run_generator(self, fixture, check=False, success=True):
        command = [
            bpy.app.binary_path,
            "--background", "--factory-startup", "--disable-autoexec",
            "--python-exit-code", "1", "--python", str(GENERATOR),
            "--", "--output", str(fixture),
        ]
        if check:
            command.append("--check")
        result = subprocess.run(command, capture_output=True, text=True, timeout=120)
        output = result.stdout + result.stderr
        if success:
            self.assertEqual(result.returncode, 0, output)
        else:
            self.assertNotEqual(result.returncode, 0, output)
        return output

    def test_reproduction_and_non_destructive_check(self):
        with TemporaryDirectory(prefix="blend-fallback-test-") as directory:
            first = Path(directory) / "first.blend"
            second = Path(directory) / "second.blend"
            self.run_generator(first)
            self.run_generator(second)
            oracle = first.with_suffix(".oracle.txt")
            expected_oracle = oracle.read_bytes()
            expected_fixture = first.read_bytes()
            self.assertEqual(expected_oracle, second.with_suffix(".oracle.txt").read_bytes())
            self.assertNotIn(str(Path.home()).encode("utf-8"), expected_fixture)
            self.run_generator(first, check=True)
            self.assertEqual(first.read_bytes(), expected_fixture)
            self.assertEqual(oracle.read_bytes(), expected_oracle)
            modified = expected_oracle.replace(b'"TextFallback" "LightFallback"',
                                               b'"TextFallback" "CameraFallback"', 1)
            self.assertNotEqual(modified, expected_oracle)
            oracle.write_bytes(modified)
            output = self.run_generator(first, check=True, success=False)
            self.assertIn("Scene oracle metadata differs", output)
            self.assertEqual(first.read_bytes(), expected_fixture)
            self.assertEqual(oracle.read_bytes(), modified)

    def test_check_inspects_saved_source(self):
        for mutation in ("data", "transform"):
            with self.subTest(mutation=mutation), TemporaryDirectory(prefix="blend-fallback-source-") as directory:
                fixture = Path(directory) / "changed.blend"
                self.run_generator(fixture)
                bpy.ops.wm.open_mainfile(filepath=str(fixture), load_ui=False, use_scripts=False)
                if mutation == "data":
                    bpy.data.objects["ImageFallback"].data = None
                else:
                    bpy.data.objects["CameraFallback"].location.x += 10.0
                bpy.context.view_layer.update()
                bpy.ops.wm.save_as_mainfile(filepath=str(fixture), compress=False, check_existing=False)
                expected = fixture.read_bytes()
                output = self.run_generator(fixture, check=True, success=False)
                self.assertIn("Fallback source kind/data changed" if mutation == "data" else
                              "Scene oracle values differ", output)
                self.assertEqual(fixture.read_bytes(), expected)


if __name__ == "__main__":
    unittest.main(argv=[__file__])

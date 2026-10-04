import subprocess
import unittest
from pathlib import Path
from tempfile import TemporaryDirectory

import bpy


GENERATOR = Path(__file__).with_name("generate_normals.py")


class NormalFixtureTests(unittest.TestCase):
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
        with TemporaryDirectory(prefix="blend-normal-test-") as directory:
            first = Path(directory) / "first"
            second = Path(directory) / "second"
            self.run_generator(first)
            self.run_generator(second)
            files = [first / f"{group}{suffix}" for group in ("smooth", "flat", "split")
                     for suffix in (".blend", ".oracle.txt")]
            saved = {path: path.read_bytes() for path in files}
            for path, contents in saved.items():
                if path.suffix == ".txt":
                    self.assertEqual(contents, (second / path.name).read_bytes())
                else:
                    self.assertNotIn(str(Path.home()).encode("utf-8"), contents)
                    self.assertNotIn(b"C:\\", contents)
            oracle = first / "smooth.oracle.txt"
            expected_oracle = oracle.read_bytes()
            self.run_generator(first, check=True)
            self.assertEqual(saved, {path: path.read_bytes() for path in files})
            modified = expected_oracle.replace(b'"Smooth"', b'"ChangedName"', 1)
            self.assertNotEqual(modified, expected_oracle)
            oracle.write_bytes(modified)
            self.assertIn("Normal oracle metadata differs",
                          self.run_generator(first, check=True, success=False))
            saved[oracle] = modified
            self.assertEqual(saved, {path: path.read_bytes() for path in files})

    def test_check_inspects_saved_normals(self):
        with TemporaryDirectory(prefix="blend-normal-content-") as directory:
            output = Path(directory) / "changed"
            self.run_generator(output)
            fixture = output / "smooth.blend"
            bpy.ops.wm.open_mainfile(filepath=str(fixture), load_ui=False, use_scripts=False)
            bpy.data.objects["Smooth"].data.polygons[0].use_smooth = False
            bpy.ops.wm.save_as_mainfile(filepath=str(fixture), compress=False, check_existing=False)
            expected = fixture.read_bytes()
            self.assertIn("Normal oracle values differ",
                          self.run_generator(output, check=True, success=False))
            self.assertEqual(expected, fixture.read_bytes())


if __name__ == "__main__":
    unittest.main(argv=[__file__])

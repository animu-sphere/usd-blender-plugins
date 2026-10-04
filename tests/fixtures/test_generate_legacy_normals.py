import subprocess
import sys
import unittest
from pathlib import Path
from tempfile import TemporaryDirectory

import bpy

sys.path.insert(0, str(Path(__file__).resolve().parent))
from generate_normals import LEGACY_ANGLES


GENERATOR = Path(__file__).with_name("generate_normals.py")


class LegacyNormalFixtureTests(unittest.TestCase):
    def run_generator(self, output, groups=tuple(LEGACY_ANGLES), check=False, success=True):
        command = [
            bpy.app.binary_path,
            "--background", "--factory-startup", "--disable-autoexec",
            "--python-exit-code", "1", "--python", str(GENERATOR),
            "--", "--output", str(output), "--groups", *groups,
        ]
        if check:
            command.append("--check")
        result = subprocess.run(command, capture_output=True, text=True, timeout=120)
        message = result.stdout + result.stderr
        self.assertEqual(result.returncode == 0, success, message)
        return message

    def test_reproduction_and_non_destructive_checks(self):
        with TemporaryDirectory(prefix="blend-legacy-normal-") as directory:
            first, second = Path(directory) / "first", Path(directory) / "second"
            self.run_generator(first)
            self.run_generator(second)
            saved = {path: path.read_bytes() for path in first.iterdir()}
            for path, contents in saved.items():
                if path.suffix == ".txt":
                    self.assertEqual(contents, (second / path.name).read_bytes())
                else:
                    self.assertEqual(contents[:12], b"BLENDER-v303")
                    self.assertNotIn(str(Path.home()).encode("utf-8"), contents)
                    self.assertNotIn(b"C:\\", contents)
            self.run_generator(first, check=True)
            self.assertEqual(saved, {path: path.read_bytes() for path in first.iterdir()})
            oracle = first / "auto_smooth.oracle.txt"
            oracle.write_bytes(saved[oracle].replace(b'"Auto_smooth"', b'"ChangedName"', 1))
            saved[oracle] = oracle.read_bytes()
            self.assertIn("Normal oracle metadata differs",
                          self.run_generator(first, check=True, success=False))
            self.assertEqual(saved, {path: path.read_bytes() for path in first.iterdir()})

    def test_check_inspects_saved_mode_angle_and_flags(self):
        with TemporaryDirectory(prefix="blend-legacy-normal-mutation-") as directory:
            for mutation in ("mode", "angle", "sharp", "flat"):
                with self.subTest(mutation=mutation):
                    output = Path(directory) / mutation
                    self.run_generator(output, groups=("auto_smooth",))
                    fixture = output / "auto_smooth.blend"
                    bpy.ops.wm.open_mainfile(filepath=str(fixture), load_ui=False, use_scripts=False)
                    mesh = bpy.data.objects["Auto_smooth"].data
                    if mutation == "mode":
                        mesh.use_auto_smooth = False
                    elif mutation == "angle":
                        mesh.auto_smooth_angle = 0.0
                    elif mutation == "sharp":
                        next(edge for edge in mesh.edges if set(edge.vertices) == {0, 1}).use_edge_sharp = True
                    else:
                        mesh.polygons[0].use_smooth = False
                    bpy.ops.wm.save_as_mainfile(filepath=str(fixture), compress=False, check_existing=False)
                    saved = {path: path.read_bytes() for path in output.iterdir()}
                    self.assertIn("Normal oracle", self.run_generator(
                        output, groups=("auto_smooth",), check=True, success=False))
                    self.assertEqual(saved, {path: path.read_bytes() for path in output.iterdir()})

    def test_selected_groups_preserve_other_files(self):
        with TemporaryDirectory(prefix="blend-legacy-normal-groups-") as directory:
            output = Path(directory)
            sentinel = output / "auto_smooth.blend"
            sentinel.write_bytes(b"unchanged")
            self.run_generator(output, groups=("auto_boundary",))
            self.assertEqual(sentinel.read_bytes(), b"unchanged")
            self.assertEqual({path.name for path in output.iterdir()},
                             {"auto_smooth.blend", "auto_boundary.blend", "auto_boundary.oracle.txt"})


if __name__ == "__main__":
    if bpy.app.version != (3, 3, 21):
        raise RuntimeError("Legacy normal regression requires Blender 3.3.21")
    unittest.main(argv=[__file__])

import subprocess
import unittest
from pathlib import Path
from tempfile import TemporaryDirectory

import bpy


GENERATOR = Path(__file__).with_name("generate_units.py")
CASES = ("unit-1m", "unit-1cm", "unit-1mm", "unit-10m")


class UnitFixtureTests(unittest.TestCase):
    def run_generator(self, directory, case=None, check=False, success=True):
        command = [
            bpy.app.binary_path,
            "--background", "--factory-startup", "--disable-autoexec",
            "--python-exit-code", "1", "--python", str(GENERATOR),
            "--", "--output-directory", str(directory),
        ]
        if case:
            command.extend(("--case", case))
        if check:
            command.append("--check")
        result = subprocess.run(command, capture_output=True, text=True, encoding="utf-8", timeout=120)
        output = result.stdout + result.stderr
        if success:
            self.assertEqual(result.returncode, 0, output)
        else:
            self.assertNotEqual(result.returncode, 0, output)
        return output

    def test_reproduction_and_non_destructive_check(self):
        with TemporaryDirectory(prefix="blend-units-test-") as directory:
            first, second = Path(directory) / "first", Path(directory) / "second"
            self.run_generator(first)
            self.run_generator(second)
            expected = {}
            for case in CASES:
                fixture = first / f"{case}.blend"
                oracle = fixture.with_suffix(".oracle.txt")
                expected[case] = (fixture.read_bytes(), oracle.read_bytes())
                self.assertEqual(expected[case][1], (second / oracle.name).read_bytes())
                self.assertNotIn(str(Path.home()).encode("utf-8"), expected[case][0])
            self.run_generator(first, check=True)
            for case, (contents, oracle) in expected.items():
                self.assertEqual(contents, (first / f"{case}.blend").read_bytes())
                self.assertEqual(oracle, (first / f"{case}.oracle.txt").read_bytes())
            oracle = first / "unit-1cm.oracle.txt"
            changed = oracle.read_bytes().replace(b'OBJECT "A B"', b'OBJECT "A C"', 1)
            self.assertNotEqual(changed, expected["unit-1cm"][1])
            oracle.write_bytes(changed)
            output = self.run_generator(first, case="unit-1cm", check=True, success=False)
            self.assertIn("Scene oracle metadata differs", output)
            self.assertEqual(expected["unit-1cm"][0], (first / "unit-1cm.blend").read_bytes())
            self.assertEqual(changed, oracle.read_bytes())

    def test_check_inspects_saved_values(self):
        for mutation in ("scale", "transform", "geometry", "name", "uv"):
            with self.subTest(mutation=mutation), TemporaryDirectory(prefix="blend-units-content-") as directory:
                directory = Path(directory)
                self.run_generator(directory, case="unit-1mm")
                fixture = directory / "unit-1mm.blend"
                oracle = fixture.with_suffix(".oracle.txt").read_bytes()
                bpy.ops.wm.open_mainfile(filepath=str(fixture), load_ui=False, use_scripts=False)
                if mutation == "scale":
                    bpy.context.scene.unit_settings.scale_length = 0.01
                elif mutation == "transform":
                    bpy.data.objects["mesh"].location.x += 100.0
                elif mutation == "geometry":
                    bpy.data.meshes["UnitCube"].vertices[0].co.x += 100.0
                elif mutation == "name":
                    bpy.data.objects["A/B"].name = "Changed"
                else:
                    bpy.data.meshes["UnitCube"].uv_layers["st"].data[0].uv.x += 0.5
                bpy.context.view_layer.update()
                bpy.ops.wm.save_as_mainfile(filepath=str(fixture), compress=False, check_existing=False)
                contents = fixture.read_bytes()
                output = self.run_generator(directory, case="unit-1mm", check=True, success=False)
                self.assertIn("Scene oracle", output)
                self.assertEqual(contents, fixture.read_bytes())
                self.assertEqual(oracle, fixture.with_suffix(".oracle.txt").read_bytes())


if __name__ == "__main__":
    unittest.main(argv=[__file__])

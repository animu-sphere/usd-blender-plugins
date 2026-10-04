import subprocess
import unittest
from pathlib import Path
from tempfile import TemporaryDirectory

import bpy


GENERATOR = Path(__file__).with_name("generate_scene.py")


class SceneFixtureTests(unittest.TestCase):
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

    def test_oracle_reproduction_and_non_destructive_check(self):
        with TemporaryDirectory(prefix="blend-scene-test-") as directory:
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
            self.assertEqual(expected_fixture, first.read_bytes())
            self.assertEqual(expected_oracle, oracle.read_bytes())
            modified = expected_oracle.replace(b'"SharedGeometry" 1', b'"IndependentGeometry" 1', 1)
            self.assertNotEqual(modified, expected_oracle)
            oracle.write_bytes(modified)
            output = self.run_generator(first, check=True, success=False)
            self.assertIn("Scene oracle metadata differs", output)
            self.assertEqual(expected_fixture, first.read_bytes())
            self.assertEqual(modified, oracle.read_bytes())

    def test_check_inspects_saved_scene(self):
        for mutation in ("transform", "sharing", "uv"):
            with self.subTest(mutation=mutation), TemporaryDirectory(prefix="blend-scene-content-") as directory:
                fixture = Path(directory) / "changed.blend"
                self.run_generator(fixture)
                bpy.ops.wm.open_mainfile(filepath=str(fixture), load_ui=False, use_scripts=False)
                if mutation == "transform":
                    bpy.data.objects["MeshParent"].location.x += 10.0
                elif mutation == "sharing":
                    bpy.data.objects["SharedChild"].data = bpy.data.meshes["IndependentGeometry"]
                else:
                    bpy.data.meshes["SharedGeometry"].uv_layers["Render"].data[0].uv.x += 0.5
                bpy.context.view_layer.update()
                bpy.ops.wm.save_as_mainfile(filepath=str(fixture), compress=False, check_existing=False)
                expected = fixture.read_bytes()
                output = self.run_generator(fixture, check=True, success=False)
                self.assertIn("Scene oracle", output)
                self.assertEqual(expected, fixture.read_bytes())


if __name__ == "__main__":
    unittest.main(argv=[__file__])

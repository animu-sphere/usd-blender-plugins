import subprocess
import unittest
from pathlib import Path
from tempfile import TemporaryDirectory

import bpy


GENERATOR = Path(__file__).with_name("generate_mesh.py")


class MeshFixtureTests(unittest.TestCase):
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
        with TemporaryDirectory(prefix="blend-mesh-test-") as directory:
            first = Path(directory) / "first.blend"
            second = Path(directory) / "second.blend"
            self.run_generator(first)
            self.run_generator(second)
            oracle = first.with_suffix(".oracle.txt")
            saved = first.read_bytes(), oracle.read_bytes()
            self.assertEqual(saved[1], second.with_suffix(".oracle.txt").read_bytes())
            self.assertNotIn(str(Path.home()).encode("utf-8"), saved[0])
            self.assertNotIn(b"C:\\", saved[0])
            self.run_generator(first, check=True)
            self.assertEqual(saved, (first.read_bytes(), oracle.read_bytes()))
            modified = saved[1].replace(b'UV "Constant" 1', b'UV "Constant" 0', 1)
            self.assertNotEqual(modified, saved[1])
            oracle.write_bytes(modified)
            self.assertIn("Scene oracle metadata differs",
                          self.run_generator(first, check=True, success=False))
            self.assertEqual((saved[0], modified), (first.read_bytes(), oracle.read_bytes()))

    def test_check_inspects_saved_mesh_domains(self):
        for mutation in ("uv", "signed_zero", "render", "loose", "sharing"):
            with self.subTest(mutation=mutation), TemporaryDirectory(prefix="blend-mesh-content-") as directory:
                fixture = Path(directory) / "changed.blend"
                self.run_generator(fixture)
                bpy.ops.wm.open_mainfile(filepath=str(fixture), load_ui=False, use_scripts=False)
                if mutation == "uv":
                    bpy.data.meshes["Seams"].uv_layers["Seams"].data[0].uv.x += 0.5
                elif mutation == "signed_zero":
                    bpy.data.meshes["Seams"].uv_layers["Seams"].data[0].uv.y = 0.0
                elif mutation == "render":
                    bpy.data.meshes["Seams"].uv_layers["Seams"].active_render = True
                elif mutation == "loose":
                    bpy.data.meshes["Loose"].vertices[0].co.z += 1.0
                else:
                    bpy.data.objects["SharedSeams"].data = bpy.data.meshes["NoUv"]
                bpy.context.view_layer.update()
                bpy.ops.wm.save_as_mainfile(filepath=str(fixture), compress=False, check_existing=False)
                saved = fixture.read_bytes()
                self.assertIn("Scene oracle", self.run_generator(fixture, check=True, success=False))
                self.assertEqual(saved, fixture.read_bytes())


if __name__ == "__main__":
    unittest.main(argv=[__file__])

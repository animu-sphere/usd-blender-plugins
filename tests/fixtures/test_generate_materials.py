import json
import subprocess
import unittest
from pathlib import Path
from tempfile import TemporaryDirectory

import bpy


GENERATOR = Path(__file__).with_name("generate_materials.py")


class MaterialFixtureTests(unittest.TestCase):
    def run_generator(self, fixture, check=False, success=True):
        command = [
            bpy.app.binary_path, "--background", "--factory-startup", "--disable-autoexec",
            "--python-exit-code", "1", "--python", str(GENERATOR),
            "--", "--output", str(fixture),
        ]
        if check:
            command.append("--check")
        result = subprocess.run(command, capture_output=True, text=True, timeout=120)
        output = result.stdout + result.stderr
        self.assertEqual(result.returncode == 0, success, output)
        return output

    def test_reproduction_and_non_destructive_check(self):
        with TemporaryDirectory(prefix="blend-material-test-") as directory:
            first = Path(directory) / "first.blend"
            second = Path(directory) / "second.blend"
            self.run_generator(first)
            self.run_generator(second)
            oracle = first.with_suffix(".oracle.json")
            expected = oracle.read_bytes()
            data = first.read_bytes()
            self.assertEqual(expected, second.with_suffix(".oracle.json").read_bytes())
            self.assertNotIn(str(Path.home()).encode("utf-8"), data)
            self.run_generator(first, check=True)
            self.assertEqual(first.read_bytes(), data)
            self.assertEqual(oracle.read_bytes(), expected)
            changed = json.loads(expected)
            changed["objects"]["Override"]["slots"][0] = "A/B"
            oracle.write_text(json.dumps(changed), encoding="ascii")
            modified = oracle.read_bytes()
            self.assertIn("Material oracle differs", self.run_generator(first, check=True, success=False))
            self.assertEqual(first.read_bytes(), data)
            self.assertEqual(oracle.read_bytes(), modified)

    def test_check_inspects_saved_source(self):
        for mutation in ("constant", "slot", "face"):
            with self.subTest(mutation=mutation), TemporaryDirectory(prefix="blend-material-source-") as directory:
                fixture = Path(directory) / "changed.blend"
                self.run_generator(fixture)
                bpy.ops.wm.open_mainfile(filepath=str(fixture), load_ui=False, use_scripts=False)
                if mutation == "constant":
                    bpy.data.materials["A_B"].node_tree.nodes["Renamed Surface"].inputs["Roughness"].default_value = 0.9
                elif mutation == "slot":
                    bpy.data.objects["Override"].material_slots[0].material = bpy.data.materials["A/B"]
                else:
                    bpy.data.objects["Multi"].data.polygons[0].material_index = 1
                bpy.ops.wm.save_as_mainfile(filepath=str(fixture), compress=False, check_existing=False)
                expected = fixture.read_bytes()
                self.assertIn("Material oracle differs", self.run_generator(fixture, check=True, success=False))
                self.assertEqual(fixture.read_bytes(), expected)


if __name__ == "__main__":
    unittest.main(argv=[__file__])

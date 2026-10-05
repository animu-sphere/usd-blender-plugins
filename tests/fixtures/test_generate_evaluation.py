import subprocess
import unittest
from pathlib import Path
from tempfile import TemporaryDirectory

import bpy


GENERATOR = Path(__file__).with_name("generate_evaluation.py")


class EvaluationFixtureTests(unittest.TestCase):
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
        with TemporaryDirectory(prefix="blend-evaluation-test-") as directory:
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
            modified = expected_oracle.replace(b'"Independent" "OutsideParent"',
                                               b'"Independent" "Root"', 1)
            self.assertNotEqual(modified, expected_oracle)
            oracle.write_bytes(modified)
            output = self.run_generator(first, check=True, success=False)
            self.assertIn("Scene oracle metadata differs", output)
            self.assertEqual(first.read_bytes(), expected_fixture)
            self.assertEqual(oracle.read_bytes(), modified)

    def test_check_inspects_saved_source(self):
        mutations = {
            "animation": "Source-only animation/frame changed",
            "constraint": "Source-only constraint changed",
            "modifier": "Source-only modifier changed",
            "shape_key": "Source-only shape keys changed",
            "parent_only": "Parent-only evaluation data changed",
            "transform": "Scene oracle values differ",
        }
        for mutation, message in mutations.items():
            with self.subTest(mutation=mutation), TemporaryDirectory(prefix="blend-evaluation-source-") as directory:
                fixture = Path(directory) / "changed.blend"
                self.run_generator(fixture)
                bpy.ops.wm.open_mainfile(filepath=str(fixture), load_ui=False, use_scripts=False)
                if mutation == "animation":
                    bpy.data.objects["Root"].animation_data_clear()
                elif mutation == "constraint":
                    bpy.data.objects["EmptyRoot"].constraints[0].mute = True
                elif mutation == "modifier":
                    bpy.data.objects["MeshParent"].modifiers[0].levels = 2
                elif mutation == "shape_key":
                    bpy.data.objects["MeshParent"].data.shape_keys.key_blocks["Raised"].value = 0
                elif mutation == "parent_only":
                    bpy.data.objects["OutsideParent"].modifiers.clear()
                else:
                    bpy.data.objects["SharedChild"].location.x += 10.0
                bpy.context.view_layer.update()
                bpy.ops.wm.save_as_mainfile(filepath=str(fixture), compress=False, check_existing=False)
                expected_fixture = fixture.read_bytes()
                oracle = fixture.with_suffix(".oracle.txt")
                expected_oracle = oracle.read_bytes()
                output = self.run_generator(fixture, check=True, success=False)
                self.assertIn(message, output)
                self.assertEqual(fixture.read_bytes(), expected_fixture)
                self.assertEqual(oracle.read_bytes(), expected_oracle)


if __name__ == "__main__":
    unittest.main(argv=[__file__])

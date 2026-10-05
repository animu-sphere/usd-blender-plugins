import subprocess
import unittest
from pathlib import Path
from tempfile import TemporaryDirectory

import bpy


GENERATOR = Path(__file__).with_name("generate_instances.py")


class InstanceFixtureTests(unittest.TestCase):
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
        with TemporaryDirectory(prefix="blend-instance-test-") as directory:
            first, second = (Path(directory) / name for name in ("first.blend", "second.blend"))
            self.run_generator(first)
            self.run_generator(second)
            oracle = first.with_suffix(".oracle.txt")
            expected_oracle, expected_fixture = oracle.read_bytes(), first.read_bytes()
            self.assertEqual(expected_oracle, second.with_suffix(".oracle.txt").read_bytes())
            self.assertNotIn(str(Path.home()).encode("utf-8"), expected_fixture)
            self.run_generator(first, check=True)
            self.assertEqual(first.read_bytes(), expected_fixture)
            self.assertEqual(oracle.read_bytes(), expected_oracle)
            modified = expected_oracle.replace(b'"TargetA" 1 1', b'"TargetB" 1 1', 1)
            self.assertNotEqual(modified, expected_oracle)
            oracle.write_bytes(modified)
            self.assertIn("Instance graph oracle differs",
                          self.run_generator(first, check=True, success=False))
            self.assertEqual(first.read_bytes(), expected_fixture)
            self.assertEqual(oracle.read_bytes(), modified)

    def test_check_inspects_saved_graph(self):
        for mutation in ("target", "active", "parent", "membership"):
            with self.subTest(mutation=mutation), TemporaryDirectory(prefix="blend-instance-source-") as directory:
                fixture = Path(directory) / "changed.blend"
                self.run_generator(fixture)
                bpy.ops.wm.open_mainfile(filepath=str(fixture), load_ui=False, use_scripts=False)
                if mutation == "target":
                    bpy.data.objects["NestedInstance"].instance_collection = bpy.data.collections["SharedTarget"]
                elif mutation == "active":
                    bpy.data.objects["RootInstance"].instance_type = "NONE"
                elif mutation == "parent":
                    bpy.data.objects["NestedLeaf"].parent = None
                else:
                    bpy.context.scene.collection.objects.link(bpy.data.objects["SharedLeaf"])
                bpy.ops.wm.save_as_mainfile(filepath=str(fixture), compress=False, check_existing=False)
                expected = fixture.read_bytes()
                self.assertIn("Instance graph oracle differs",
                              self.run_generator(fixture, check=True, success=False))
                self.assertEqual(fixture.read_bytes(), expected)


if __name__ == "__main__":
    unittest.main(argv=[__file__])

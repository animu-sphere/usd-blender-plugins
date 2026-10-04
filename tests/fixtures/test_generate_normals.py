import subprocess
import struct
import unittest
from pathlib import Path
from tempfile import TemporaryDirectory

import bpy


GENERATOR = Path(__file__).with_name("generate_normals.py")
GROUPS = ("smooth", "flat", "split", "custom", "multi")


class NormalFixtureTests(unittest.TestCase):
    def check_multi_addresses(self, fixture):
        contents = fixture.read_bytes()
        modern = bpy.app.version >= (5, 0, 0)
        header = struct.Struct("<4siQqq" if modern else "<4siQii")
        offset = 17 if modern else 12
        self.assertEqual(contents[:offset], b"BLENDER17-01v0502" if modern else b"BLENDER-v405")
        addresses = {}
        while offset < len(contents):
            self.assertGreaterEqual(len(contents) - offset, header.size)
            code, first, address, second, count = header.unpack_from(contents, offset)
            dna, size = (first, second) if modern else (second, first)
            offset += header.size
            self.assertGreaterEqual(size, 0)
            self.assertGreaterEqual(count, 0)
            self.assertGreaterEqual(len(contents) - offset, size)
            if code == b"DATA" and address:
                addresses.setdefault(address, []).append((dna, size, count, contents[offset:offset + size]))
            offset += size
        differing = []
        for records in addresses.values():
            if len(records) > 1:
                self.assertTrue(modern, "Independently constructed legacy meshes must have unique addresses")
                self.assertEqual(len({record[:3] for record in records}), 1)
                if len({record[3] for record in records}) > 1:
                    differing.append(records[0][1:3])
        if modern:
            self.assertIn((216, 9), differing, "Attribute records collide with differing payloads")
            self.assertIn((32, 1), differing, "AttributeArray records collide with differing payloads")

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
            self.check_multi_addresses(first / "multi.blend")
            self.check_multi_addresses(second / "multi.blend")
            files = [first / f"{group}{suffix}" for group in GROUPS
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

    def test_check_inspects_saved_custom_normals(self):
        with TemporaryDirectory(prefix="blend-normal-custom-") as directory:
            output = Path(directory) / "changed"
            self.run_generator(output)
            fixture = output / "custom.blend"
            bpy.ops.wm.open_mainfile(filepath=str(fixture), load_ui=False, use_scripts=False)
            mesh = bpy.data.objects["Custom"].data
            mesh.normals_split_custom_set([(1.0, 0.0, 0.0)] * len(mesh.loops))
            bpy.ops.wm.save_as_mainfile(filepath=str(fixture), compress=False, check_existing=False)
            expected = fixture.read_bytes()
            self.assertIn("Normal oracle values differ",
                          self.run_generator(output, check=True, success=False))
            self.assertEqual(expected, fixture.read_bytes())

    def test_selected_groups_leave_other_files_unchanged(self):
        with TemporaryDirectory(prefix="blend-normal-groups-") as directory:
            output = Path(directory)
            sentinel = output / "smooth.blend"
            sentinel.write_bytes(b"unchanged")
            command = [
                bpy.app.binary_path,
                "--background", "--factory-startup", "--disable-autoexec",
                "--python-exit-code", "1", "--python", str(GENERATOR),
                "--", "--output", str(output), "--groups", "custom", "multi",
            ]
            result = subprocess.run(command, capture_output=True, text=True, timeout=120)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertEqual(sentinel.read_bytes(), b"unchanged")
            self.assertEqual({path.name for path in output.iterdir()},
                             {"smooth.blend", "custom.blend", "custom.oracle.txt",
                              "multi.blend", "multi.oracle.txt"})

    def test_check_inspects_saved_multi_geometry(self):
        with TemporaryDirectory(prefix="blend-normal-multi-") as directory:
            output = Path(directory) / "changed"
            self.run_generator(output)
            fixture = output / "multi.blend"
            bpy.ops.wm.open_mainfile(filepath=str(fixture), load_ui=False, use_scripts=False)
            bpy.data.objects["Other"].data.vertices[0].co.z += 0.5
            bpy.ops.wm.save_as_mainfile(filepath=str(fixture), compress=False, check_existing=False)
            expected = fixture.read_bytes()
            self.assertIn("Normal oracle values differ",
                          self.run_generator(output, check=True, success=False))
            self.assertEqual(expected, fixture.read_bytes())


if __name__ == "__main__":
    unittest.main(argv=[__file__])

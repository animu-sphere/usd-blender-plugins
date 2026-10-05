import json
import subprocess
import unittest
from pathlib import Path
from tempfile import TemporaryDirectory

import bpy


GENERATOR = Path(__file__).with_name("generate_textures.py")


class TextureFixtureTests(unittest.TestCase):
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
        with TemporaryDirectory(prefix="blend-texture-test-") as directory:
            first = Path(directory) / "first" / "textures.blend"
            second = Path(directory) / "second" / "textures.blend"
            self.run_generator(first)
            self.run_generator(second)
            oracle = first.with_suffix(".oracle.json")
            expected = oracle.read_bytes()
            data = first.read_bytes()
            image = (first.parent / "textures" / "color.png").read_bytes()
            self.assertEqual(expected, second.with_suffix(".oracle.json").read_bytes())
            self.assertEqual(image, (second.parent / "textures" / "color.png").read_bytes())
            for home in (str(Path.home()), Path.home().as_posix()):
                self.assertNotIn(home.encode("utf-8"), data)
            self.run_generator(first, check=True)
            self.assertEqual(first.read_bytes(), data)
            self.assertEqual(oracle.read_bytes(), expected)
            self.assertEqual((first.parent / "textures" / "color.png").read_bytes(), image)
            changed = json.loads(expected)
            changed["materials"]["NamedUV"]["texture"]["uvMap"] = "Render"
            oracle.write_text(json.dumps(changed), encoding="ascii")
            modified = oracle.read_bytes()
            self.assertIn("Texture oracle differs", self.run_generator(first, check=True, success=False))
            self.assertEqual(first.read_bytes(), data)
            self.assertEqual(oracle.read_bytes(), modified)
            self.assertEqual((first.parent / "textures" / "color.png").read_bytes(), image)

    def test_check_inspects_saved_source(self):
        for mutation in ("path", "wrap", "uv", "strength", "image", "normal_space"):
            with self.subTest(mutation=mutation), TemporaryDirectory(prefix="blend-texture-source-") as directory:
                fixture = Path(directory) / "textures.blend"
                self.run_generator(fixture)
                bpy.ops.wm.open_mainfile(filepath=str(fixture), load_ui=False, use_scripts=False)
                texture = bpy.data.materials["Relative"].node_tree.nodes["Renamed Image"]
                normal = next(node for node in bpy.data.materials["Normal"].node_tree.nodes
                              if node.type == "NORMAL_MAP")
                if mutation == "path":
                    texture.image.filepath = "//different.png"
                elif mutation == "wrap":
                    texture.extension = "CLIP"
                elif mutation == "uv":
                    next(node for node in bpy.data.materials["NamedUV"].node_tree.nodes
                         if node.type == "UVMAP").uv_map = "Render"
                elif mutation == "strength":
                    normal.inputs["Strength"].default_value = 0.5
                elif mutation == "image":
                    texture.image = None
                else:
                    normal.space = "OBJECT"
                bpy.ops.wm.save_as_mainfile(filepath=str(fixture), compress=False,
                                          check_existing=False, relative_remap=False)
                expected = fixture.read_bytes()
                self.assertIn("Texture oracle differs", self.run_generator(fixture, check=True, success=False))
                self.assertEqual(fixture.read_bytes(), expected)


if __name__ == "__main__":
    unittest.main(argv=[__file__])

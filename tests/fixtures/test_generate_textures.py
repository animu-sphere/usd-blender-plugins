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
        for mutation in ("path", "wrap", "uv", "strength", "image", "normal_space", "colorspace"):
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
                elif mutation == "normal_space":
                    normal.space = "OBJECT"
                else:
                    texture.image.colorspace_settings.name = "Non-Color"
                bpy.ops.wm.save_as_mainfile(filepath=str(fixture), compress=False,
                                          check_existing=False, relative_remap=False)
                expected = fixture.read_bytes()
                self.assertIn("Texture oracle differs", self.run_generator(fixture, check=True, success=False))
                self.assertEqual(fixture.read_bytes(), expected)

    def test_rendered_color_and_socket_coercions(self):
        image_path = (Path(__file__).parent / "native-textures" / "blender-5.2.2" /
                      "textures" / "color.png")
        with TemporaryDirectory(prefix="blend-texture-render-") as directory:
            bpy.ops.wm.read_factory_settings(use_empty=True)
            scene = bpy.context.scene
            scene.render.engine = "CYCLES"
            scene.cycles.device = "CPU"
            scene.cycles.samples = 16
            scene.cycles.use_adaptive_sampling = False
            scene.cycles.use_denoising = False
            scene.render.resolution_x = 8
            scene.render.resolution_y = 8
            scene.render.resolution_percentage = 100
            scene.render.image_settings.file_format = "OPEN_EXR"
            scene.render.image_settings.color_mode = "RGBA"
            scene.render.image_settings.color_depth = "32"
            scene.render.film_transparent = True
            scene.view_settings.view_transform = "Standard"
            bpy.ops.object.camera_add(location=(0, 0, 3))
            scene.camera = bpy.context.object
            scene.camera.data.type = "ORTHO"
            scene.camera.data.ortho_scale = 1
            bpy.ops.mesh.primitive_plane_add(size=2)
            obj = bpy.context.object
            material = bpy.data.materials.new("TextureRenderOracle")
            obj.data.materials.append(material)
            nodes = material.node_tree.nodes
            nodes.clear()
            image = bpy.data.images.load(str(image_path), check_existing=False)
            texture = nodes.new("ShaderNodeTexImage")
            texture.image = image
            texture.interpolation = "Linear"
            coordinates = nodes.new("ShaderNodeCombineXYZ")
            coordinates.inputs["X"].default_value = 0.75
            coordinates.inputs["Y"].default_value = 0.25
            emission = nodes.new("ShaderNodeEmission")
            output = nodes.new("ShaderNodeOutputMaterial")
            links = material.node_tree.links
            links.new(coordinates.outputs["Vector"], texture.inputs["Vector"])
            links.new(emission.outputs["Emission"], output.inputs["Surface"])
            alpha = 128 / 255
            linear_alpha = ((alpha + 0.055) / 1.055) ** 2.4
            cases = (
                ("srgb_color", "sRGB", (("Color", "Color"),), (0, linear_alpha, 0)),
                ("srgb_color_and_alpha", "sRGB", (("Color", "Color"), ("Alpha", "Strength")), (0, alpha, 0)),
                ("raw_color", "Non-Color", (("Color", "Color"),), (0, 1, 0)),
                ("alpha_to_color", "Non-Color", (("Alpha", "Color"),), (alpha, alpha, alpha)),
                ("color_to_scalar", "Non-Color", (("Color", "Strength"),), (0.7151684,) * 3),
            )
            for name, colorspace, connections, expected in cases:
                with self.subTest(case=name):
                    image.colorspace_settings.name = colorspace
                    for socket in emission.inputs:
                        for link in list(socket.links):
                            links.remove(link)
                    emission.inputs["Color"].default_value = (1, 1, 1, 1)
                    emission.inputs["Strength"].default_value = 1
                    for source, target in connections:
                        links.new(texture.outputs[source], emission.inputs[target])
                    path = Path(directory) / f"{name}.exr"
                    scene.render.filepath = str(path)
                    bpy.ops.render.render(write_still=True)
                    rendered = bpy.data.images.load(str(path), check_existing=False)
                    index = (4 * rendered.size[0] + 4) * 4
                    actual = list(rendered.pixels[index:index + 3])
                    bpy.data.images.remove(rendered)
                    for value, reference in zip(actual, expected):
                        self.assertAlmostEqual(value, reference, delta=0.0001)


if __name__ == "__main__":
    unittest.main(argv=[__file__])

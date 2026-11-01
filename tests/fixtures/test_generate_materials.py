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
        for mutation in ("constant", "emission_color", "emission_strength", "slot", "face"):
            with self.subTest(mutation=mutation), TemporaryDirectory(prefix="blend-material-source-") as directory:
                fixture = Path(directory) / "changed.blend"
                self.run_generator(fixture)
                bpy.ops.wm.open_mainfile(filepath=str(fixture), load_ui=False, use_scripts=False)
                if mutation == "constant":
                    bpy.data.materials["A_B"].node_tree.nodes["Renamed Surface"].inputs["Roughness"].default_value = 0.9
                elif mutation == "emission_color":
                    bpy.data.materials["Deferred"].node_tree.nodes["Principled BSDF"].inputs["Emission Color"].default_value = (0.25, 0.5, 0.75, 1)
                elif mutation == "emission_strength":
                    bpy.data.materials["Deferred"].node_tree.nodes["Principled BSDF"].inputs["Emission Strength"].default_value = 2
                elif mutation == "slot":
                    bpy.data.objects["Override"].material_slots[0].material = bpy.data.materials["A/B"]
                else:
                    bpy.data.objects["Multi"].data.polygons[0].material_index = 1
                bpy.ops.wm.save_as_mainfile(filepath=str(fixture), compress=False, check_existing=False)
                expected = fixture.read_bytes()
                self.assertIn("Material oracle differs", self.run_generator(fixture, check=True, success=False))
                self.assertEqual(fixture.read_bytes(), expected)

    def test_principled_alpha_and_emission_render(self):
        fixture = (Path(__file__).parent / "native-materials" / "blender-5.2.2" /
                   "materials.blend")
        with TemporaryDirectory(prefix="blend-material-render-") as directory:
            bpy.ops.wm.open_mainfile(filepath=str(fixture), load_ui=False, use_scripts=False)
            material = bpy.data.materials["Deferred"]
            surface = material.node_tree.nodes.get("Principled BSDF")
            self.assertAlmostEqual(surface.inputs["Alpha"].default_value, 0.25)
            self.assertAlmostEqual(surface.inputs["Emission Strength"].default_value, 3.0)
            scene = bpy.data.scenes.new("MaterialRenderOracle")
            bpy.context.window.scene = scene
            scene.render.engine = "CYCLES"
            scene.cycles.device = "CPU"
            scene.cycles.samples = 1024
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
            bpy.context.object.data.materials.append(material)
            surface.inputs["Base Color"].default_value = (0, 0, 0, 1)
            surface.inputs["Emission Color"].default_value = (0.25, 0.5, 0.75, 1)
            for alpha, strength in ((1.0, 0.0), (1.0, 1.0), (1.0, 3.0),
                                    (0.25, 3.0), (0.0, 3.0)):
                with self.subTest(alpha=alpha, strength=strength):
                    surface.inputs["Alpha"].default_value = alpha
                    surface.inputs["Emission Strength"].default_value = strength
                    path = Path(directory) / f"{alpha}-{strength}.exr"
                    scene.render.filepath = str(path)
                    bpy.ops.render.render(write_still=True)
                    rendered = bpy.data.images.load(str(path), check_existing=False)
                    pixels = list(rendered.pixels)
                    bpy.data.images.remove(rendered)
                    average = [sum(pixels[channel::4]) / (len(pixels) // 4)
                               for channel in range(4)]
                    expected_rgb = (0.25 * strength * alpha, 0.5 * strength * alpha,
                                    0.75 * strength * alpha)
                    for actual, expected in zip(average[:3], expected_rgb):
                        self.assertAlmostEqual(actual, expected, delta=0.03)
                    self.assertAlmostEqual(average[3], alpha, delta=0.02)


if __name__ == "__main__":
    unittest.main(argv=[__file__])

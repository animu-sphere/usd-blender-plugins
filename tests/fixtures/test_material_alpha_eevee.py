import unittest
from pathlib import Path
from tempfile import TemporaryDirectory

import bpy


FIXTURE = (Path(__file__).parent / "native-materials" / "blender-5.2.2" /
           "materials.blend")


class MaterialAlphaEeveeTests(unittest.TestCase):
    def test_legacy_blend_method_alias(self):
        self.assertEqual(bpy.app.version, (5, 2, 2))
        bpy.ops.wm.open_mainfile(filepath=str(FIXTURE), load_ui=False, use_scripts=False)
        material = bpy.data.materials["Deferred"]
        for legacy, expected in (("OPAQUE", "DITHERED"), ("CLIP", "DITHERED"),
                                 ("HASHED", "DITHERED"), ("BLEND", "BLENDED")):
            with self.subTest(legacy=legacy):
                material.surface_render_method = "DITHERED"
                material.blend_method = legacy
                self.assertEqual(material.surface_render_method, expected)
                self.assertEqual(material.blend_method,
                                 "BLEND" if expected == "BLENDED" else "HASHED")

    def test_saved_render_methods_and_thresholds(self):
        self.assertEqual(bpy.app.version, (5, 2, 2))
        with TemporaryDirectory(prefix="blend-material-eevee-") as directory:
            for method in ("DITHERED", "BLENDED"):
                for threshold in (0.1, 0.9):
                    for alpha in (0.25, 0.75):
                        with self.subTest(method=method, threshold=threshold, alpha=alpha):
                            bpy.ops.wm.open_mainfile(filepath=str(FIXTURE), load_ui=False,
                                                     use_scripts=False)
                            material = bpy.data.materials["Deferred"]
                            surface = material.node_tree.nodes["Principled BSDF"]
                            material.surface_render_method = method
                            material.alpha_threshold = threshold
                            surface.inputs["Alpha"].default_value = alpha
                            surface.inputs["Base Color"].default_value = (0, 0, 0, 1)
                            surface.inputs["Emission Color"].default_value = (0.25, 0.5, 0.75, 1)
                            surface.inputs["Emission Strength"].default_value = 3

                            scene = bpy.data.scenes.new("MaterialEeveeOracle")
                            bpy.context.window.scene = scene
                            scene.render.engine = "BLENDER_EEVEE"
                            scene.eevee.taa_render_samples = 256
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

                            stem = f"{method}-{threshold}-{alpha}"
                            saved = Path(directory) / f"{stem}.blend"
                            image_path = Path(directory) / f"{stem}.exr"
                            scene.render.filepath = str(image_path)
                            bpy.ops.wm.save_as_mainfile(filepath=str(saved), compress=False,
                                                        check_existing=False)
                            bpy.ops.wm.open_mainfile(filepath=str(saved), load_ui=False,
                                                     use_scripts=False)
                            material = bpy.data.materials["Deferred"]
                            self.assertEqual(material.surface_render_method, method)
                            self.assertAlmostEqual(material.alpha_threshold, threshold)
                            self.assertAlmostEqual(
                                material.node_tree.nodes["Principled BSDF"].inputs["Alpha"].default_value,
                                alpha,
                            )
                            bpy.ops.render.render(write_still=True)
                            rendered = bpy.data.images.load(str(image_path), check_existing=False)
                            pixels = list(rendered.pixels)
                            bpy.data.images.remove(rendered)
                            self.assertEqual(len(pixels), 8 * 8 * 4)
                            average = [sum(pixels[channel::4]) / (len(pixels) // 4)
                                       for channel in range(4)]
                            for actual, color in zip(average[:3], (0.25, 0.5, 0.75)):
                                self.assertAlmostEqual(actual, color * 3 * alpha, delta=0.03)
                            self.assertAlmostEqual(average[3], alpha, delta=0.02)


if __name__ == "__main__":
    unittest.main(argv=[__file__])

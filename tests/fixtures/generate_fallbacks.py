import argparse
import sys
from pathlib import Path
from tempfile import TemporaryDirectory

import bpy
from mathutils import Matrix


sys.path.insert(0, str(Path(__file__).resolve().parent))
from generate_scene import compare_oracle, make_scene, oracle_text


ROOT = Path(__file__).resolve().parent / "native-scene"
VERSION = (5, 2, 2)


def make_fallbacks():
    make_scene()
    scene = bpy.context.scene
    scene.name = "Fallbacks"

    def add(name, data, parent):
        obj = bpy.data.objects.new(name, data)
        scene.collection.objects.link(obj)
        obj.parent = parent
        obj.location = (3.25, -1.5, 2.75)
        obj.rotation_euler = (0.13, -0.29, 0.47)
        obj.scale = (-1.25, 0.75, 2.0)
        obj.matrix_parent_inverse = Matrix(
            ((1.0, 0.2, 0.0, -1.0), (0.0, 0.75, -0.1, 2.0),
             (0.3, 0.0, 1.25, -3.0), (0.0, 0.0, 0.0, 1.0))
        )
        return obj

    camera_data = bpy.data.cameras.new("CameraData")
    camera = add("CameraFallback", camera_data, bpy.data.objects["Root"])
    light = add("LightFallback", bpy.data.lights.new("LightData", "POINT"), camera)
    light.hide_render = True
    text = add("TextFallback", bpy.data.curves.new("TextData", "FONT"), light)
    bpy.data.objects["SharedRoot"].parent = text
    image = add("ImageFallback", None, bpy.data.objects["MeshParent"])
    image.empty_display_type = "IMAGE"
    image.data = bpy.data.images.new("ImageData", width=1, height=1)
    old_parent = bpy.data.objects["OutsideParent"]
    outside = bpy.data.objects.new("OutsideCamera", camera_data)
    bpy.data.scenes["Unselected"].collection.objects.link(outside)
    for channel in ("location", "delta_location", "rotation_euler", "delta_rotation_euler",
                    "scale", "delta_scale"):
        setattr(outside, channel, getattr(old_parent, channel).copy())
    bpy.data.objects["Independent"].parent = outside
    bpy.data.objects.remove(old_parent, do_unlink=True)
    outside.name = "OutsideParent"
    bpy.context.view_layer.update()


def checked_oracle():
    expected_types = {
        "CameraFallback": "CAMERA", "LightFallback": "LIGHT",
        "TextFallback": "FONT", "ImageFallback": "EMPTY", "OutsideParent": "CAMERA",
    }
    for name, kind in expected_types.items():
        obj = bpy.data.objects[name]
        if obj.type != kind or obj.data is None:
            raise RuntimeError(f"Fallback source kind/data changed: {name}")
    if not isinstance(bpy.data.objects["ImageFallback"].data, bpy.types.Image):
        raise RuntimeError("Image fallback must retain Image data")
    return oracle_text(scene_name="Fallbacks")


def save_fallbacks(output):
    make_fallbacks()
    output.parent.mkdir(parents=True, exist_ok=True)
    bpy.ops.wm.save_as_mainfile(filepath=str(output), compress=False, check_existing=False)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, default=ROOT / "blender-5.2.2" / "fallbacks.blend")
    parser.add_argument("--check", action="store_true")
    arguments = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    options = parser.parse_args(arguments)
    if bpy.app.version != VERSION:
        raise RuntimeError(f"Unsupported fixture Blender version: {bpy.app.version_string}")
    output = options.output.resolve()
    oracle = output.with_suffix(".oracle.txt")
    if options.check:
        expected = oracle.read_text(encoding="ascii")
        with TemporaryDirectory(prefix="blend-fallbacks-") as directory:
            save_fallbacks(Path(directory) / "fallbacks.blend")
            compare_oracle(expected, checked_oracle())
    else:
        save_fallbacks(output)
        expected = checked_oracle()
        oracle.write_text(expected, encoding="ascii")
    bpy.ops.wm.open_mainfile(filepath=str(output), load_ui=False, use_scripts=False)
    compare_oracle(expected, checked_oracle())
    print(f"Verified saved fallback oracle with Blender {bpy.app.version_string}: {output}")


if __name__ == "__main__":
    main()

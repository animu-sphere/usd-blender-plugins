import argparse
import math
import sys
from pathlib import Path
from tempfile import TemporaryDirectory

import bpy
from mathutils import Matrix


VERSIONS = {(4, 5, 13), (5, 2, 2)}
ROOT = Path(__file__).resolve().parent / "native-transforms"
MODES = ("XYZ", "XZY", "YXZ", "YZX", "ZXY", "ZYX", "QUATERNION", "AXIS_ANGLE")


def add_object(name, mode):
    obj = bpy.data.objects.new(name, None)
    bpy.context.scene.collection.objects.link(obj)
    obj.rotation_mode = mode
    obj.location = (1.25, -2.5, 3.75)
    obj.delta_location = (-0.5, 0.25, 0.75)
    obj.scale = (2.0, -3.0, 0.5)
    obj.delta_scale = (0.75, 1.25, -2.0)
    obj.rotation_euler = (0.23, -0.41, 0.67)
    obj.delta_rotation_euler = (-0.31, 0.19, 0.53)
    obj.rotation_quaternion = (2.0, 0.3, -0.5, 0.7)
    obj.delta_rotation_quaternion = (1.5, -0.2, 0.4, 0.1)
    obj.rotation_axis_angle = (0.73, 0.2, -0.3, 0.4)
    return obj


def make_scene():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    for screen in bpy.data.screens:
        for area in screen.areas:
            for space in area.spaces:
                if space.type == "FILE_BROWSER" and space.params is not None:
                    space.params.directory = b"//"
    bpy.context.preferences.filepaths.save_version = 0
    scene = bpy.context.scene
    scene.name = "Transforms"
    scene.unit_settings.scale_length = 0.01
    parent = None
    for mode in MODES:
        root = add_object(f"Root_{mode}", mode)
        root.hide_render = mode == "XYZ"
        if parent is None:
            parent = root
        child = add_object(f"Child_{mode}", mode)
        child.parent = parent
        child.matrix_parent_inverse = Matrix(
            ((1.0, 0.2, 0.0, -1.0), (0.0, 0.75, -0.1, 2.0),
             (0.3, 0.0, 1.25, -3.0), (0.0, 0.0, 0.0, 1.0))
        )
        child.location = (-3.0, 5.0, 7.0)
        grandchild = add_object(f"Grandchild_{mode}", "XYZ")
        grandchild.parent = child
        grandchild.matrix_parent_inverse = Matrix.Translation((0.5, -1.5, 2.5))
    zero_quat = add_object("ZeroQuaternion", "QUATERNION")
    zero_quat.rotation_quaternion = (0.0, 0.0, 0.0, 0.0)
    zero_quat.delta_rotation_quaternion = (0.0, 0.0, 0.0, 0.0)
    zero_axis = add_object("ZeroAxis", "AXIS_ANGLE")
    zero_axis.rotation_axis_angle = (0.73, 0.0, 0.0, 0.0)
    zero_scale = add_object("ZeroScale", "XYZ")
    zero_scale.scale = (0.0, -3.0, 0.5)
    bpy.context.view_layer.update()


def oracle_text():
    scene = bpy.context.scene
    if scene.name != "Transforms" or len(bpy.data.scenes) != 1:
        raise RuntimeError("Transform fixture must contain one active Scene named Transforms")
    bpy.context.view_layer.update()
    objects = sorted(scene.objects, key=lambda obj: obj.name)
    rows = [
        "BLEND_TRANSFORMS_ORACLE 1",
        f"{bpy.app.version_string!r}",
        f"{scene.unit_settings.scale_length:.17g} {len(objects)}",
    ]
    for obj in objects:
        parent = obj.parent.name if obj.parent else ""
        rows.append(f'"{obj.name}" "{parent}" {int(obj.hide_render)}')
        for matrix in (obj.matrix_world, obj.matrix_local):
            rows.append(" ".join(f"{value:.17g}" for row in matrix for value in row))
    return "\n".join(rows) + "\n"


def compare_oracle(expected, actual):
    expected_lines = expected.splitlines()
    actual_lines = actual.splitlines()
    if len(expected_lines) != len(actual_lines):
        raise RuntimeError("Transform oracle record count differs")
    for index, (left, right) in enumerate(zip(expected_lines, actual_lines)):
        if index < 3 or (index - 3) % 3 == 0:
            if left != right:
                raise RuntimeError(f"Transform oracle metadata differs at line {index + 1}")
        else:
            expected_values = list(map(float, left.split()))
            actual_values = list(map(float, right.split()))
            if len(expected_values) != 16 or len(actual_values) != 16:
                raise RuntimeError("Transform oracle matrix must contain 16 values")
            if not all(
                math.isfinite(value) and math.isclose(value, reference, rel_tol=2e-6, abs_tol=2e-6)
                for reference, value in zip(expected_values, actual_values)
            ):
                raise RuntimeError(f"Transform oracle matrix differs at line {index + 1}")


def save_scene(output):
    make_scene()
    output.parent.mkdir(parents=True, exist_ok=True)
    bpy.ops.wm.save_as_mainfile(filepath=str(output), compress=False, check_existing=False)


def main():
    parser = argparse.ArgumentParser()
    version = ".".join(map(str, bpy.app.version))
    parser.add_argument("--output", type=Path, default=ROOT / f"blender-{version}" / "transforms.blend")
    parser.add_argument("--check", action="store_true")
    arguments = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    options = parser.parse_args(arguments)
    if bpy.app.version not in VERSIONS:
        raise RuntimeError(f"Unsupported fixture Blender version: {bpy.app.version_string}")
    output = options.output.resolve()
    oracle = output.with_suffix(".oracle.txt")
    if options.check:
        expected = oracle.read_text(encoding="ascii")
        with TemporaryDirectory(prefix="blend-transforms-") as directory:
            save_scene(Path(directory) / "transforms.blend")
            compare_oracle(expected, oracle_text())
    else:
        save_scene(output)
        expected = oracle_text()
        oracle.write_text(expected, encoding="ascii")
    bpy.ops.wm.open_mainfile(filepath=str(output), load_ui=False, use_scripts=False)
    compare_oracle(expected, oracle_text())
    print(f"Verified saved transform oracle with Blender {bpy.app.version_string}: {output}")


if __name__ == "__main__":
    main()

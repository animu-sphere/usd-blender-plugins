import argparse
import math
import shlex
import sys
from pathlib import Path
from tempfile import TemporaryDirectory

import bpy
from mathutils import Matrix


VERSIONS = {(4, 5, 13), (5, 2, 2)}
ROOT = Path(__file__).resolve().parent / "native-scene"


def make_mesh(name, height):
    mesh = bpy.data.meshes.new(name)
    mesh.from_pydata(
        [(0, 0, 0), (2, 0, 0), (2, 1, 0), (0, 1, 0), (0, 0, height)],
        [], [(0, 1, 2, 3), (1, 0, 4)],
    )
    mesh.polygons[0].use_smooth = True
    for name, offset in (("Detail", 0.0), ("Render", 0.25)):
        layer = mesh.uv_layers.new(name=name)
        for index, loop in enumerate(mesh.loops):
            layer.data[index].uv = (
                loop.vertex_index * 0.25 + offset,
                loop.vertex_index % 2 if name == "Detail" else index % 2,
            )
        layer.active_render = name == "Render"
    mesh.update()
    return mesh


def make_scene():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    for screen in bpy.data.screens:
        for area in screen.areas:
            for space in area.spaces:
                if space.type == "FILE_BROWSER" and space.params is not None:
                    space.params.directory = b"//"
    bpy.context.preferences.filepaths.save_version = 0
    scene = bpy.context.scene
    scene.name = "Integrated"
    scene.unit_settings.scale_length = 0.01
    nested = bpy.data.collections.new("Nested")
    scene.collection.children.link(nested)
    other_scene = bpy.data.scenes.new("Unselected")
    shared = make_mesh("SharedGeometry", 1.5)
    independent = make_mesh("IndependentGeometry", 2.5)
    parent_only = make_mesh("ParentOnlyGeometry", 3.5)

    def add(name, data=None, parent=None, collection=None):
        obj = bpy.data.objects.new(name, data)
        (collection if collection is not None else scene.collection).objects.link(obj)
        obj.parent = parent
        obj.rotation_mode = "XYZ"
        obj.location = (1.25, -2.5, 3.75)
        obj.delta_location = (-0.5, 0.25, 0.75)
        obj.rotation_euler = (0.23, -0.41, 0.67)
        obj.delta_rotation_euler = (-0.31, 0.19, 0.53)
        obj.scale = (2.0, -3.0, 0.5)
        obj.delta_scale = (0.75, 1.25, -2.0)
        if parent is not None:
            obj.matrix_parent_inverse = Matrix(
                ((1.0, 0.2, 0.0, -1.0), (0.0, 0.75, -0.1, 2.0),
                 (0.3, 0.0, 1.25, -3.0), (0.0, 0.0, 0.0, 1.0))
            )
        return obj

    root = add("Root")
    root.hide_render = True
    mesh_parent = add("MeshParent", shared, root)
    empty_child = add("mesh", parent=mesh_parent, collection=nested)
    child = add("SharedChild", shared, empty_child, nested)
    child.hide_render = True
    add("SharedRoot", shared, collection=nested)
    add("EmptyRoot")
    outside = add("OutsideParent", parent_only, collection=other_scene.collection)
    outside.location = (-4.0, 2.0, 1.0)
    outside.scale = (0.5, 2.0, -1.0)
    add("Independent", independent, outside, nested)
    nested.objects.link(mesh_parent)
    bpy.context.view_layer.update()


def oracle_text(scene_name="Integrated", scene_count=2):
    scene = bpy.context.scene
    if scene.name != scene_name or len(bpy.data.scenes) != scene_count:
        raise RuntimeError(f"Scene fixture must retain active {scene_name} and {scene_count} Scenes")
    bpy.context.view_layer.update()
    objects = sorted(scene.objects, key=lambda obj: obj.name)
    meshes = sorted({obj.data for obj in objects if obj.type == "MESH"}, key=lambda mesh: mesh.name)
    rows = [
        "BLEND_SCENE_ORACLE 1",
        repr(bpy.app.version_string),
        f"{scene.unit_settings.scale_length:.17g} {len(objects)} {len(meshes)}",
    ]
    for obj in objects:
        parent = obj.parent.name if obj.parent else ""
        data = obj.data.name if obj.type == "MESH" else ""
        rows.append(f'OBJECT "{obj.name}" "{parent}" "{data}" {int(obj.hide_render)}')
        for label, matrix in (("WORLD", obj.matrix_world), ("LOCAL", obj.matrix_local)):
            rows.append(label + " " + " ".join(f"{value:.17g}" for row in matrix for value in row))
    for mesh in meshes:
        rows.append(
            f'MESH "{mesh.name}" {len(mesh.vertices)} {len(mesh.polygons)} '
            f'{len(mesh.loops)} {len(mesh.uv_layers)}'
        )
        rows.extend("POINT " + " ".join(f"{value:.17g}" for value in vertex.co) for vertex in mesh.vertices)
        rows.append("COUNTS " + " ".join(str(face.loop_total) for face in mesh.polygons))
        rows.append("INDICES " + " ".join(str(loop.vertex_index) for loop in mesh.loops))
        rows.extend("NORMAL " + " ".join(f"{value:.17g}" for value in normal.vector) for normal in mesh.corner_normals)
        for layer in mesh.uv_layers:
            rows.append(f'UV "{layer.name}" {int(layer.active_render)}')
            rows.extend("VALUE " + " ".join(f"{value:.17g}" for value in entry.uv) for entry in layer.data)
    return "\n".join(rows) + "\n"


def compare_oracle(expected, actual):
    left_rows, right_rows = expected.splitlines(), actual.splitlines()
    if len(left_rows) != len(right_rows):
        raise RuntimeError("Scene oracle record count differs")
    for index, (left, right) in enumerate(zip(left_rows, right_rows)):
        values, references = shlex.split(right), shlex.split(left)
        if references and references[0] in ("WORLD", "LOCAL", "POINT", "NORMAL", "VALUE"):
            if len(values) != len(references) or values[0] != references[0] or not all(
                math.isfinite(float(value)) and math.isclose(
                    float(value), float(reference), rel_tol=2e-6, abs_tol=2e-6
                )
                for reference, value in zip(references[1:], values[1:])
            ):
                raise RuntimeError(f"Scene oracle values differ at line {index + 1}")
        elif left != right:
            raise RuntimeError(f"Scene oracle metadata differs at line {index + 1}")


def save_scene(output):
    make_scene()
    output.parent.mkdir(parents=True, exist_ok=True)
    bpy.ops.wm.save_as_mainfile(filepath=str(output), compress=False, check_existing=False)


def main():
    parser = argparse.ArgumentParser()
    version = ".".join(map(str, bpy.app.version))
    parser.add_argument("--output", type=Path, default=ROOT / f"blender-{version}" / "scene.blend")
    parser.add_argument("--check", action="store_true")
    arguments = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    options = parser.parse_args(arguments)
    if bpy.app.version not in VERSIONS:
        raise RuntimeError(f"Unsupported fixture Blender version: {bpy.app.version_string}")
    output = options.output.resolve()
    oracle = output.with_suffix(".oracle.txt")
    if options.check:
        expected = oracle.read_text(encoding="ascii")
        with TemporaryDirectory(prefix="blend-scene-") as directory:
            save_scene(Path(directory) / "scene.blend")
            compare_oracle(expected, oracle_text())
    else:
        save_scene(output)
        expected = oracle_text()
        oracle.write_text(expected, encoding="ascii")
    bpy.ops.wm.open_mainfile(filepath=str(output), load_ui=False, use_scripts=False)
    compare_oracle(expected, oracle_text())
    print(f"Verified saved Scene oracle with Blender {bpy.app.version_string}: {output}")


if __name__ == "__main__":
    main()

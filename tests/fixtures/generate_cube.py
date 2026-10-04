import argparse
import sys
from pathlib import Path
from tempfile import TemporaryDirectory

import bpy


DEFAULT_OUTPUT = (
    Path(__file__).resolve().parents[2]
    / "plugins/usdBlendFileFormat/tests/fixtures/single_cube.blend"
)


def validate_cube():
    if len(bpy.data.scenes) != 1 or bpy.context.scene.name != "Scene":
        raise RuntimeError("Cube fixture requires one active Scene")
    if bpy.context.scene.unit_settings.scale_length != 1.0:
        raise RuntimeError("Cube fixture requires unit scale 1")
    if len(bpy.data.objects) != 1 or len(bpy.data.meshes) != 1:
        raise RuntimeError("Cube fixture requires exactly one Object and Mesh")
    obj = bpy.data.objects["Cube"]
    mesh = obj.data
    if obj.parent is not None or obj.hide_render:
        raise RuntimeError("Cube must be an unparented visible Mesh")
    if any(
        obj.matrix_world[row][column] != float(row == column)
        for row in range(4) for column in range(4)
    ):
        raise RuntimeError("Cube requires an identity world transform")
    expected = {(x, y, z) for x in (-1.0, 1.0) for y in (-1.0, 1.0) for z in (-1.0, 1.0)}
    if {tuple(vertex.co) for vertex in mesh.vertices} != expected:
        raise RuntimeError("Cube points must span two meters on each axis")
    if len(mesh.polygons) != 6 or any(len(face.vertices) != 4 for face in mesh.polygons):
        raise RuntimeError("Cube requires six quads")
    if len(mesh.loops) != 24 or len(mesh.uv_layers) != 1:
        raise RuntimeError("Cube requires 24 corners and one UV map")
    if not mesh.uv_layers[0].active_render:
        raise RuntimeError("Cube UV map must be the render map")


def cube_snapshot():
    validate_cube()
    mesh = bpy.data.objects["Cube"].data
    return (
        [tuple(vertex.co) for vertex in mesh.vertices],
        [tuple(face.vertices) for face in mesh.polygons],
        [tuple(normal.vector) for normal in mesh.corner_normals],
        [tuple(value.uv) for value in mesh.uv_layers[0].data],
    )


def save_cube(output):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    for screen in bpy.data.screens:
        for area in screen.areas:
            for space in area.spaces:
                if space.type == "FILE_BROWSER" and space.params is not None:
                    space.params.directory = b"//"
    bpy.context.preferences.filepaths.save_version = 0
    bpy.ops.mesh.primitive_cube_add(size=2.0)
    validate_cube()
    output.parent.mkdir(parents=True, exist_ok=True)
    bpy.ops.wm.save_as_mainfile(filepath=str(output), compress=False, check_existing=False)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument("--check", action="store_true")
    arguments = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    options = parser.parse_args(arguments)
    if bpy.app.version != (5, 2, 2):
        raise RuntimeError(f"Use Blender 5.2.2, not {bpy.app.version_string}")
    output = options.output.resolve()
    if options.check:
        with TemporaryDirectory(prefix="blend-cube-") as directory:
            save_cube(Path(directory) / "single_cube.blend")
            expected = cube_snapshot()
    else:
        save_cube(output)
        expected = cube_snapshot()
    if output.read_bytes()[:17] != b"BLENDER17-01v0502":
        raise RuntimeError("Cube fixture must be uncompressed format-1")
    bpy.ops.wm.open_mainfile(filepath=str(output), load_ui=False, use_scripts=False)
    if cube_snapshot() != expected:
        raise RuntimeError("Saved Cube geometry, normals or UVs differ from regeneration")
    print(f"Verified cube fixture with Blender {bpy.app.version_string}: {output}")


if __name__ == "__main__":
    main()

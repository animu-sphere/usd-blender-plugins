import argparse
import sys
from pathlib import Path
from tempfile import TemporaryDirectory

import bpy
from mathutils import Matrix


sys.path.insert(0, str(Path(__file__).resolve().parent))
from generate_scene import VERSIONS, compare_oracle, oracle_text


ROOT = Path(__file__).resolve().parent / "native-units"
CASES = {
    "unit-1m": (1.0, "NONE"),
    "unit-1cm": (0.01, "METRIC"),
    "unit-1mm": (0.001, "IMPERIAL"),
    "unit-10m": (10.0, "METRIC"),
}


def make_scene(scale, system):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    for screen in bpy.data.screens:
        for area in screen.areas:
            for space in area.spaces:
                if space.type == "FILE_BROWSER" and space.params is not None:
                    space.params.directory = b"//"
    bpy.context.preferences.filepaths.save_version = 0
    scene = bpy.context.scene
    scene.name = "Units"
    scene.unit_settings.system = system
    scene.unit_settings.scale_length = scale
    bpy.data.scenes.new("Unselected").unit_settings.scale_length = 7.0
    width = 1.0 / scale
    mesh = bpy.data.meshes.new("UnitCube")
    mesh.from_pydata(
        [(x * width / 2, y * width / 2, z * width / 2)
         for x, y, z in ((-1, -1, -1), (1, -1, -1), (1, 1, -1), (-1, 1, -1),
                        (-1, -1, 1), (1, -1, 1), (1, 1, 1), (-1, 1, 1))],
        [], [(0, 3, 2, 1), (4, 5, 6, 7), (0, 1, 5, 4),
             (1, 2, 6, 5), (2, 3, 7, 6), (3, 0, 4, 7)],
    )
    for index, name in enumerate(("A/B", "A_B", "st", "Render Map", "\u65e5\u672c\u8a9e")):
        layer = mesh.uv_layers.new(name=name)
        for corner, entry in enumerate(layer.data):
            u, v = ((0, 0), (1, 0), (1, 1), (0, 1))[corner % 4]
            entry.uv = (u + index * 0.25, v)
        layer.active_render = name == "Render Map"
    mesh.update()

    def add(name, data=None, parent=None):
        obj = bpy.data.objects.new(name, data)
        scene.collection.objects.link(obj)
        obj.parent = parent
        return obj

    add("Cube", mesh)
    translated = add("Translated", mesh)
    translated.location = tuple(value / scale for value in (2, -3, 4))
    parent = add("Parent", mesh)
    parent.location = tuple(value / scale for value in (5, -2, 1))
    parent.rotation_euler = (0.2, -0.4, 0.6)
    parent.scale = (-2, 3, 0.5)
    child = add("mesh", mesh, parent)
    child.location = tuple(value / scale for value in (1, 2, 3))
    child.delta_location = tuple(value / scale for value in (0.25, -0.5, 0.75))
    child.rotation_euler = (-0.3, 0.5, 0.1)
    child.scale = (0.5, 1.25, -1.5)
    child.matrix_parent_inverse = Matrix(
        ((1, 0.2, 0, 0.5 / scale), (0, 0.75, -0.1, -1 / scale),
         (0.3, 0, 1.25, 1.5 / scale), (0, 0, 0, 1))
    )
    for name in ("mesh.1", "mesh_1"):
        add(name, parent=parent)
    for name in ("A B", "A/B", "A_B", "A_B_1", "Cube.001", "Cube_001",
                 "3D Text", "Object", "\u65e5\u672c\u8a9e", "\u65e5\u672c\u8a9e2"):
        add(name)
    bpy.context.view_layer.update()


def unit_oracle_text():
    rows = [oracle_text(scene_name="Units").rstrip("\n")]
    scene = bpy.context.scene
    rows.append(f"UNIT_SYSTEM {scene.unit_settings.system}")
    for obj in sorted(scene.objects, key=lambda obj: obj.name):
        if obj.type == "MESH":
            rows.append(f'WORLD_MESH "{obj.name}" {len(obj.data.vertices)}')
            rows.extend(
                "POINT " + " ".join(f"{value:.17g}" for value in obj.matrix_world @ vertex.co)
                for vertex in obj.data.vertices
            )
    return "\n".join(rows) + "\n"


def save_scene(output, scale, system):
    make_scene(scale, system)
    output.parent.mkdir(parents=True, exist_ok=True)
    bpy.ops.wm.save_as_mainfile(filepath=str(output), compress=False, check_existing=False)


def main():
    parser = argparse.ArgumentParser()
    version = ".".join(map(str, bpy.app.version))
    parser.add_argument("--output-directory", type=Path, default=ROOT / f"blender-{version}")
    parser.add_argument("--case", choices=CASES)
    parser.add_argument("--check", action="store_true")
    arguments = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    options = parser.parse_args(arguments)
    if bpy.app.version not in VERSIONS:
        raise RuntimeError(f"Unsupported fixture Blender version: {bpy.app.version_string}")
    for case in (options.case,) if options.case else CASES:
        scale, system = CASES[case]
        output = options.output_directory.resolve() / f"{case}.blend"
        oracle = output.with_suffix(".oracle.txt")
        if options.check:
            expected = oracle.read_text(encoding="utf-8")
            with TemporaryDirectory(prefix="blend-units-") as directory:
                save_scene(Path(directory) / output.name, scale, system)
                compare_oracle(expected, unit_oracle_text())
        else:
            save_scene(output, scale, system)
            expected = unit_oracle_text()
            oracle.write_text(expected, encoding="utf-8")
        bpy.ops.wm.open_mainfile(filepath=str(output), load_ui=False, use_scripts=False)
        compare_oracle(expected, unit_oracle_text())
        print(f"Verified saved unit oracle with Blender {bpy.app.version_string}: {output}")


if __name__ == "__main__":
    main()

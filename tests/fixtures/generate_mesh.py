import argparse
import sys
from pathlib import Path
from tempfile import TemporaryDirectory

import bpy


sys.path.insert(0, str(Path(__file__).resolve().parent))
from generate_scene import VERSIONS, compare_oracle, oracle_text, reset_scene


ROOT = Path(__file__).resolve().parent / "native-mesh"
MESH_VERSIONS = VERSIONS | {(3, 3, 21)}


def make_scene():
    scene = reset_scene()
    scene.name = "MeshDomains"
    scene.unit_settings.scale_length = 0.01

    def add(name, points, edges, faces):
        mesh = bpy.data.meshes.new(name)
        mesh.from_pydata(points, edges, faces)
        obj = bpy.data.objects.new(name, mesh)
        scene.collection.objects.link(obj)
        return mesh

    mesh = add(
        "Seams",
        [(0, 0, 0), (2, 0, 0), (2, 1, 0), (0, 1, 0), (0, 0, 1.5)],
        [], [(0, 1, 2, 3), (1, 0, 4)],
    )
    seam_values = [(0.0, -0.0), (2.0, -1.0), (2.0, 3.0), (-0.0, 0.0),
                   (-2.0, 0.5), (1.0, 2.0), (2.0, -1.0)]
    for name in ("Seams", "Constant"):
        layer = mesh.uv_layers.new(name=name)
        for entry, value in zip(layer.data, seam_values):
            entry.uv = value if name == "Seams" else (-0.25, 1.5)
        layer.active_render = name == "Constant"
    mesh.uv_layers.active_index = 0
    if bpy.app.version == (3, 3, 21):
        mesh.polygons[0].use_smooth = True
        mesh.edges[0].use_edge_sharp = True
    mesh.update()
    shared = bpy.data.objects.new("SharedSeams", mesh)
    shared.location = (10, -20, 30)
    shared.scale = (2, 3, 4)
    scene.collection.objects.link(shared)
    add("NoUv", [(0, 0, 0), (1, 0, 0), (0, 1, 0)], [], [(0, 1, 2)])
    loose = add("Loose", [(0, 0, 0), (1, 2, 3), (-1, 4, 2)], [(0, 1)], [])
    loose.uv_layers.new(name="EmptyUv")
    empty = add("Empty", [], [], [])
    empty.uv_layers.new(name="EmptyUv")
    bpy.context.view_layer.update()


def saved_oracle():
    return oracle_text(scene_name="MeshDomains", scene_count=1)


def save_scene(output):
    make_scene()
    output.parent.mkdir(parents=True, exist_ok=True)
    bpy.ops.wm.save_as_mainfile(filepath=str(output), compress=False, check_existing=False)


def main():
    parser = argparse.ArgumentParser()
    version = ".".join(map(str, bpy.app.version))
    parser.add_argument("--output", type=Path, default=ROOT / f"blender-{version}" / "mesh.blend")
    parser.add_argument("--check", action="store_true")
    arguments = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    options = parser.parse_args(arguments)
    if bpy.app.version not in MESH_VERSIONS:
        raise RuntimeError(f"Unsupported fixture Blender version: {bpy.app.version_string}")
    output = options.output.resolve()
    oracle = output.with_suffix(".oracle.txt")
    if options.check:
        expected = oracle.read_text(encoding="ascii")
        with TemporaryDirectory(prefix="blend-mesh-") as directory:
            save_scene(Path(directory) / "mesh.blend")
            compare_oracle(expected, saved_oracle())
    else:
        save_scene(output)
        expected = saved_oracle()
        oracle.write_text(expected, encoding="ascii")
    bpy.ops.wm.open_mainfile(filepath=str(output), load_ui=False, use_scripts=False)
    compare_oracle(expected, saved_oracle())
    print(f"Verified saved Mesh domain oracle with Blender {bpy.app.version_string}: {output}")


if __name__ == "__main__":
    main()

import argparse
import sys
from pathlib import Path
from tempfile import TemporaryDirectory

import bpy


sys.path.insert(0, str(Path(__file__).resolve().parent))
from generate_scene import compare_oracle, make_scene, oracle_text


ROOT = Path(__file__).resolve().parent / "native-scene"
VERSION = (5, 2, 2)
CONSTRAINTS = {"EmptyRoot": "MeshParent", "MeshParent": "Root", "OutsideParent": "EmptyRoot"}


def make_evaluation():
    make_scene()
    scene = bpy.context.scene
    scene.name = "SourceOnly"
    root = bpy.data.objects["Root"]
    root.keyframe_insert(data_path="location", frame=1)
    root.location.x += 8.0
    root.keyframe_insert(data_path="location", frame=10)
    scene.frame_set(1)
    for name, target in CONSTRAINTS.items():
        constraint = bpy.data.objects[name].constraints.new("COPY_LOCATION")
        constraint.name = "SourceOnlyLocation"
        constraint.target = bpy.data.objects[target]
    parent = bpy.data.objects["MeshParent"]
    modifier = parent.modifiers.new("SourceOnlySubdivision", "SUBSURF")
    modifier.levels = modifier.render_levels = 1
    parent.shape_key_add(name="Basis")
    key = parent.shape_key_add(name="Raised")
    key.data[0].co.z += 2.0
    key.value = 0.65
    outside = bpy.data.objects["OutsideParent"]
    outside.modifiers.new("ParentOnlySubdivision", "SUBSURF").levels = 1
    outside.shape_key_add(name="Basis")
    outside.shape_key_add(name="ParentOnlyRaised").value = 0.5
    bpy.context.view_layer.update()


def checked_oracle():
    scene = bpy.context.scene
    root = bpy.data.objects["Root"]
    if scene.frame_current != 1 or root.animation_data is None or root.animation_data.action is None:
        raise RuntimeError("Source-only animation/frame changed")
    if tuple(root.location) != (1.25, -2.5, 3.75):
        raise RuntimeError("Source-only saved animation channels changed")
    try:
        scene.frame_set(10)
        if tuple(root.location) != (9.25, -2.5, 3.75):
            raise RuntimeError("Source-only animation keys changed")
    finally:
        scene.frame_set(1)
    if tuple(root.location) != (1.25, -2.5, 3.75):
        raise RuntimeError("Source-only animation keys changed")
    constraints = []
    for name, target in CONSTRAINTS.items():
        saved = bpy.data.objects[name].constraints
        if len(saved) != 1:
            raise RuntimeError(f"Source-only constraint changed: {name}")
        constraint = saved[0]
        if (constraint.name != "SourceOnlyLocation" or constraint.type != "COPY_LOCATION" or
                constraint.target != bpy.data.objects[target] or constraint.mute or
                constraint.influence != 1.0):
            raise RuntimeError(f"Source-only constraint changed: {name}")
        constraints.append(constraint)
    parent = bpy.data.objects["MeshParent"]
    modifiers = parent.modifiers
    if (len(modifiers) != 1 or modifiers[0].name != "SourceOnlySubdivision" or
            modifiers[0].type != "SUBSURF" or modifiers[0].levels != 1 or
            modifiers[0].render_levels != 1 or not modifiers[0].show_viewport or
            not modifiers[0].show_render):
        raise RuntimeError("Source-only modifier changed")
    keys = parent.data.shape_keys
    if (keys is None or list(keys.key_blocks.keys()) != ["Basis", "Raised"] or
            abs(keys.key_blocks["Raised"].value - 0.65) > 1e-6 or
            tuple(keys.key_blocks["Raised"].data[0].co) != (0.0, 0.0, 2.0)):
        raise RuntimeError("Source-only shape keys changed")
    outside = bpy.data.objects["OutsideParent"]
    if (len(outside.modifiers) != 1 or outside.modifiers[0].type != "SUBSURF" or
            outside.data.shape_keys is None or
            list(outside.data.shape_keys.key_blocks.keys()) != ["Basis", "ParentOnlyRaised"] or
            outside.data.shape_keys.key_blocks["ParentOnlyRaised"].value != 0.5):
        raise RuntimeError("Parent-only evaluation data changed")
    bpy.context.view_layer.update()
    depsgraph = bpy.context.evaluated_depsgraph_get()
    if len(parent.evaluated_get(depsgraph).data.vertices) <= len(parent.data.vertices):
        raise RuntimeError("Fixture modifier must change evaluated topology")
    shared = bpy.data.objects["SharedRoot"]
    evaluated = shared.evaluated_get(depsgraph).data
    if tuple(evaluated.vertices[0].co) == tuple(shared.data.vertices[0].co):
        raise RuntimeError("Fixture shape key must change evaluated positions")
    evaluated_world = parent.matrix_world.copy()
    # Ask Blender for source-channel matrices with constraints temporarily muted.
    try:
        for constraint in constraints:
            constraint.mute = True
        bpy.context.view_layer.update()
        if evaluated_world == parent.matrix_world:
            raise RuntimeError("Fixture constraint must change evaluated transforms")
        return oracle_text(scene_name="SourceOnly")
    finally:
        for constraint in constraints:
            constraint.mute = False
        bpy.context.view_layer.update()


def save_evaluation(output):
    make_evaluation()
    output.parent.mkdir(parents=True, exist_ok=True)
    bpy.ops.wm.save_as_mainfile(filepath=str(output), compress=False, check_existing=False)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, default=ROOT / "blender-5.2.2" / "evaluation.blend")
    parser.add_argument("--check", action="store_true")
    arguments = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    options = parser.parse_args(arguments)
    if bpy.app.version != VERSION:
        raise RuntimeError(f"Unsupported fixture Blender version: {bpy.app.version_string}")
    output = options.output.resolve()
    oracle = output.with_suffix(".oracle.txt")
    if options.check:
        expected = oracle.read_text(encoding="ascii")
        with TemporaryDirectory(prefix="blend-evaluation-") as directory:
            save_evaluation(Path(directory) / "evaluation.blend")
            compare_oracle(expected, checked_oracle())
    else:
        save_evaluation(output)
        expected = checked_oracle()
        oracle.write_text(expected, encoding="ascii")
    bpy.ops.wm.open_mainfile(filepath=str(output), load_ui=False, use_scripts=False)
    compare_oracle(expected, checked_oracle())
    print(f"Verified saved source-only oracle with Blender {bpy.app.version_string}: {output}")


if __name__ == "__main__":
    main()

import argparse
import sys
from pathlib import Path
from tempfile import TemporaryDirectory

import bpy


sys.path.insert(0, str(Path(__file__).resolve().parent))
from generate_scene import reset_scene


ROOT = Path(__file__).resolve().parent / "native-scene" / "blender-5.2.2"


def make_instances():
    scene = reset_scene()
    scene.name = "Instances"
    outside = bpy.data.scenes.new("Unselected")
    collections = {
        name: bpy.data.collections.new(name)
        for name in ("TargetA", "TargetB", "SharedTarget", "ParentTarget")
    }
    collections["TargetA"].children.link(collections["SharedTarget"])
    collections["TargetB"].children.link(collections["SharedTarget"])

    def add(name, owner, target=None, active=False, parent=None):
        obj = bpy.data.objects.new(name, None)
        owner.objects.link(obj)
        obj.parent = parent
        obj.location = (1.0, 2.0, 3.0)
        if target:
            obj.instance_collection = collections[target]
        obj.instance_type = "COLLECTION" if active else "NONE"
        return obj

    parent = add("OutsideParent", outside.collection, "ParentTarget")
    add("RootInstance", scene.collection, "TargetA", True)
    add("SharedInstance", scene.collection, "TargetA", True)
    add("Child", scene.collection, parent=parent)
    add("NestedInstance", collections["TargetA"], "TargetB", True)
    instance_parent = add("InstanceParent", outside.collection, "ParentTarget")
    add("NestedLeaf", collections["TargetB"], parent=instance_parent)
    add("SharedLeaf", collections["SharedTarget"])
    add("ParentLeaf", collections["ParentTarget"])
    bpy.context.view_layer.update()


def oracle_text():
    scene = bpy.context.scene
    if scene.name != "Instances" or len(bpy.data.scenes) != 2:
        raise RuntimeError("Instance fixture must retain active Instances and two Scenes")
    collections = sorted([scene.collection, *bpy.data.collections], key=lambda value: value.name)
    objects = sorted(bpy.data.objects, key=lambda value: value.name)
    rows = [
        "BLEND_INSTANCE_ORACLE 1",
        f'"{bpy.app.version_string}" "{scene.name}" {len(collections)} {len(objects)}',
    ]
    for collection in collections:
        children = [child.name for child in collection.children]
        members = [obj.name for obj in collection.objects]
        rows.append(
            f'COLLECTION "{collection.name}" {len(children)} {len(members)}'
            + "".join(f' "{name}"' for name in children + members)
        )
    for obj in objects:
        if obj.type != "EMPTY" or obj.data is not None:
            raise RuntimeError(f"Instance source kind/data changed: {obj.name}")
        parent = obj.parent.name if obj.parent else ""
        target = obj.instance_collection.name if obj.instance_collection else ""
        rows.append(
            f'OBJECT "{obj.name}" "{parent}" "{target}" '
            f'{int(obj.instance_type == "COLLECTION")} {int(obj.name in scene.objects)}'
        )
    return "\n".join(rows) + "\n"


def compare_oracle(expected):
    if expected != oracle_text():
        raise RuntimeError("Instance graph oracle differs")


def save_instances(output):
    make_instances()
    output.parent.mkdir(parents=True, exist_ok=True)
    bpy.ops.wm.save_as_mainfile(filepath=str(output), compress=False, check_existing=False)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, default=ROOT / "instances.blend")
    parser.add_argument("--check", action="store_true")
    arguments = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    options = parser.parse_args(arguments)
    if bpy.app.version != (5, 2, 2):
        raise RuntimeError(f"Unsupported fixture Blender version: {bpy.app.version_string}")
    output = options.output.resolve()
    oracle = output.with_suffix(".oracle.txt")
    if options.check:
        expected = oracle.read_text(encoding="ascii")
        with TemporaryDirectory(prefix="blend-instances-") as directory:
            save_instances(Path(directory) / "instances.blend")
            compare_oracle(expected)
    else:
        save_instances(output)
        expected = oracle_text()
        oracle.write_text(expected, encoding="ascii")
    bpy.ops.wm.open_mainfile(filepath=str(output), load_ui=False, use_scripts=False)
    compare_oracle(expected)
    print(f"Verified saved instance graph with Blender {bpy.app.version_string}: {output}")


if __name__ == "__main__":
    main()

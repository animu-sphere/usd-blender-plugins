import argparse
import json
import sys
from pathlib import Path
from tempfile import TemporaryDirectory

import bpy


sys.path.insert(0, str(Path(__file__).resolve().parent))
from generate_scene import reset_scene


ROOT = Path(__file__).resolve().parent / "native-materials" / "blender-5.2.2"


def make_materials():
    scene = reset_scene()
    scene.name = "Materials"

    def material(name):
        value = bpy.data.materials.new(name)
        value.diffuse_color = (0.125, 0.25, 0.5, 1.0)
        value.metallic = 0.25
        value.roughness = 0.75
        return value

    viewport = material("A/B")
    surface = viewport.node_tree.nodes.get("Principled BSDF")
    surface.inputs["Base Color"].default_value = viewport.diffuse_color
    surface.inputs["Metallic"].default_value = viewport.metallic
    surface.inputs["Roughness"].default_value = viewport.roughness
    principled = material("A_B")
    surface = principled.node_tree.nodes.get("Principled BSDF")
    surface.name = "Renamed Surface"
    output = principled.node_tree.nodes.get("Material Output")
    alternate = principled.node_tree.nodes.new("ShaderNodeOutputMaterial")
    alternate.is_active_output = False
    output.is_active_output = True
    for name, value in {
        "Base Color": (0.6, 0.3, 0.15, 1.0), "Metallic": 0.8,
        "Roughness": 0.2, "IOR": 1.7, "Coat Weight": 0.4, "Coat Roughness": 0.1,
    }.items():
        surface.inputs[name].default_value = value
    linked = material("Linked")
    surface = linked.node_tree.nodes.get("Principled BSDF")
    surface.inputs["Base Color"].default_value = (0.2, 0.4, 0.6, 1.0)
    rgb = linked.node_tree.nodes.new("ShaderNodeRGB")
    linked.node_tree.links.new(rgb.outputs[0], surface.inputs["Base Color"])
    unsupported = material("Unsupported")
    unsupported.node_tree.nodes.get("Principled BSDF").inputs["Transmission Weight"].default_value = 0.5
    deferred = material("Deferred")
    surface = deferred.node_tree.nodes.get("Principled BSDF")
    surface.inputs["Alpha"].default_value = 0.25
    surface.inputs["Emission Strength"].default_value = 3.0
    fallback = material("Fallback")
    tree = fallback.node_tree
    tree.nodes.remove(tree.nodes.get("Principled BSDF"))
    diffuse = tree.nodes.new("ShaderNodeBsdfDiffuse")
    tree.links.new(diffuse.outputs[0], tree.nodes.get("Material Output").inputs["Surface"])
    muted = material("Muted")
    muted.node_tree.nodes.get("Principled BSDF").mute = True
    muted_link = material("MutedLink")
    muted_link.node_tree.links[0].is_muted = True

    def mesh(name, slots, indices):
        data = bpy.data.meshes.new(name + "Data")
        data.from_pydata(
            [(0, 0, 0), (1, 0, 0), (1, 1, 0), (0, 1, 0), (0.5, 0.5, 1)],
            [], [(0, 1, 4), (1, 2, 4), (2, 3, 4), (3, 0, 4)],
        )
        for slot in slots:
            data.materials.append(slot)
        for face, index in zip(data.polygons, indices):
            face.material_index = index
        obj = bpy.data.objects.new(name, data)
        scene.collection.objects.link(obj)
        return obj

    mesh("Single", [viewport], [0, 0, 0, 0])
    multi = mesh("Multi", [viewport, principled], [0, 1, 1, 0])
    override = bpy.data.objects.new("Override", multi.data)
    scene.collection.objects.link(override)
    override.material_slots[0].link = "OBJECT"
    override.material_slots[0].material = linked
    override.material_slots[1].link = "OBJECT"
    override.material_slots[1].material = None
    mesh("EmptySlot", [viewport, None, principled], [0, 1, 2, 1])
    mesh("Unbound", [], [0, 0, 0, 0])
    for name, value in (
        ("Unsupported", unsupported), ("Deferred", deferred), ("Fallback", fallback),
        ("Muted", muted), ("MutedLink", muted_link),
    ):
        mesh(name, [value], [0, 0, 0, 0])
    bpy.context.view_layer.update()


def oracle():
    scene = bpy.context.scene
    if scene.name != "Materials" or len(bpy.data.scenes) != 1:
        raise RuntimeError("Material fixture must retain one active Materials scene")
    materials = {}
    for material in sorted(bpy.data.materials, key=lambda value: value.name):
        record = {
            "viewport": list(material.diffuse_color[:3]),
            "metallic": material.metallic, "roughness": material.roughness,
            "ior": 1.5, "clearcoat": 0.0, "clearcoatRoughness": 0.03,
            "diagnostics": [],
        }
        if material.node_tree:
            output = next(node for node in material.node_tree.nodes
                          if node.type == "OUTPUT_MATERIAL" and node.is_active_output)
            links = list(output.inputs["Surface"].links)
            surface = links[0].from_node if len(links) == 1 and not links[0].is_muted else None
            if surface is None or surface.type != "BSDF_PRINCIPLED" or surface.mute:
                record["diagnostics"].append("BLEND_MATERIAL_UNSUPPORTED_NODE")
            else:
                record["viewport"] = list(surface.inputs["Base Color"].default_value[:3])
                for source, target in (
                    ("Metallic", "metallic"), ("Roughness", "roughness"), ("IOR", "ior"),
                    ("Coat Weight", "clearcoat"), ("Coat Roughness", "clearcoatRoughness"),
                ):
                    record[target] = surface.inputs[source].default_value
                if surface.inputs["Base Color"].is_linked:
                    record["diagnostics"].append("BLEND_MATERIAL_UNSUPPORTED_NODE")
                if surface.inputs["Transmission Weight"].default_value != 0:
                    record["diagnostics"].append("BLEND_MATERIAL_UNSUPPORTED_INPUT")
                if surface.inputs["Alpha"].default_value != 1 or surface.inputs["Emission Strength"].default_value != 0:
                    record["diagnostics"].append("BLEND_MATERIAL_DEFERRED_INPUT")
        materials[material.name] = record
    objects = {
        obj.name: {
            "mesh": obj.data.name,
            "slots": [slot.material.name if slot.material else None for slot in obj.material_slots],
            "faces": [face.material_index for face in obj.data.polygons],
        }
        for obj in sorted(scene.objects, key=lambda value: value.name)
    }
    return {"version": bpy.app.version_string, "scene": scene.name,
            "materials": materials, "objects": objects}


def compare(expected):
    if expected != oracle():
        raise RuntimeError("Material oracle differs")


def save(output):
    make_materials()
    output.parent.mkdir(parents=True, exist_ok=True)
    bpy.ops.wm.save_as_mainfile(filepath=str(output), compress=False, check_existing=False)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, default=ROOT / "materials.blend")
    parser.add_argument("--check", action="store_true")
    arguments = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    options = parser.parse_args(arguments)
    if bpy.app.version != (5, 2, 2):
        raise RuntimeError(f"Unsupported fixture Blender version: {bpy.app.version_string}")
    output = options.output.resolve()
    oracle_path = output.with_suffix(".oracle.json")
    if options.check:
        expected = json.loads(oracle_path.read_text(encoding="ascii"))
        with TemporaryDirectory(prefix="blend-materials-") as directory:
            save(Path(directory) / "materials.blend")
            compare(expected)
    else:
        save(output)
        expected = oracle()
        oracle_path.write_text(json.dumps(expected, indent=2, sort_keys=True) + "\n", encoding="ascii")
    bpy.ops.wm.open_mainfile(filepath=str(output), load_ui=False, use_scripts=False)
    compare(expected)
    print(f"Verified saved material oracle with Blender {bpy.app.version_string}: {output}")


if __name__ == "__main__":
    main()

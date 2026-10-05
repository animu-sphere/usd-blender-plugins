import argparse
import json
import sys
from pathlib import Path
from tempfile import TemporaryDirectory

import bpy


sys.path.insert(0, str(Path(__file__).resolve().parent))
from generate_scene import reset_scene


ROOT = Path(__file__).resolve().parent / "native-textures" / "blender-5.2.2"


def make_textures(output):
    scene = reset_scene()
    scene.name = "Textures"
    directory = output.parent / "textures"
    directory.mkdir(parents=True, exist_ok=True)
    pixels = bpy.data.images.new("Pixels", width=2, height=2, alpha=True)
    pixels.pixels[:] = [
        1, 0, 0, 1, 0, 1, 0, 0.5,
        0, 0, 1, 0.25, 0.5, 0.5, 1, 1,
    ]
    pixels.filepath_raw = str(directory / "color.png")
    pixels.file_format = "PNG"
    pixels.save()
    bpy.data.images.remove(pixels)

    def material(name, extension="REPEAT", interpolation="Linear", uv=None,
                 source="FILE", path="//textures/color.png", normal=False,
                 strength=1, space="TANGENT", colorspace="sRGB", scalar=""):
        value = bpy.data.materials.new(name)
        surface = value.node_tree.nodes.get("Principled BSDF")
        surface.name = "Renamed Surface"
        surface.inputs["Base Color"].default_value = (0.2, 0.4, 0.6, 1)
        surface.inputs["Roughness"].default_value = 0.3
        image = bpy.data.images.load(str(directory / "color.png"), check_existing=False)
        image.name = name + "Image"
        image.colorspace_settings.name = colorspace
        image.filepath = path
        if source == "PACKED":
            data = (directory / "color.png").read_bytes()
            image.pack(data=data, data_len=len(data))
        else:
            image.source = source
        texture = value.node_tree.nodes.new("ShaderNodeTexImage")
        texture.name = "Renamed Image"
        texture.image = image
        texture.extension = extension
        texture.interpolation = interpolation
        if uv:
            reader = value.node_tree.nodes.new("ShaderNodeUVMap")
            reader.uv_map = uv
            value.node_tree.links.new(reader.outputs["UV"], texture.inputs["Vector"])
        if normal:
            node = value.node_tree.nodes.new("ShaderNodeNormalMap")
            node.space = space
            node.uv_map = uv or ""
            node.inputs["Strength"].default_value = strength
            value.node_tree.links.new(texture.outputs["Color"], node.inputs["Color"])
            value.node_tree.links.new(node.outputs["Normal"], surface.inputs["Normal"])
        else:
            value.node_tree.links.new(
                texture.outputs["Alpha" if scalar else "Color"],
                surface.inputs[scalar or "Base Color"],
            )
        data = bpy.data.meshes.new(name + "Data")
        data.from_pydata([(0, 0, 0), (1, 0, 0), (1, 1, 0), (0, 1, 0)],
                        [], [(0, 1, 2, 3)])
        for uv_name in ("Detail/UV", "Detail_UV", "Render"):
            layer = data.uv_layers.new(name=uv_name)
            for index, corner in enumerate(layer.data):
                corner.uv = ((index % 2) * 0.5, (index // 2) * 0.5)
            layer.active_render = uv_name == "Render"
        data.materials.append(value)
        obj = bpy.data.objects.new(name, data)
        scene.collection.objects.link(obj)
        return value

    material("Relative")
    material("NamedUV", extension="EXTEND", uv="Detail_UV")
    material("ActiveUV", extension="CLIP", uv="Render")
    material("Mirror", extension="MIRROR")
    material("Closest", interpolation="Closest")
    material("Absolute", path=r"C:\usd-blend-fixtures\color.png")
    material("UNC", path=r"\\usd-blend-fixtures\textures\color.png")
    material("Raw", colorspace="Non-Color")
    material("AlphaScalar", scalar="Roughness")
    for name, socket in (("MetallicAlpha", "Metallic"), ("IorAlpha", "IOR"),
                         ("ClearcoatAlpha", "Coat Weight"),
                         ("ClearcoatRoughnessAlpha", "Coat Roughness")):
        material(name, scalar=socket)
    material("Normal", normal=True, colorspace="Non-Color")
    material("NamedNormal", normal=True, uv="Detail_UV", colorspace="Non-Color")
    material("NormalStrength", normal=True, strength=0.5, colorspace="Non-Color")
    material("ObjectNormal", normal=True, space="OBJECT", colorspace="Non-Color")
    for name, source in (("Packed", "PACKED"), ("Generated", "GENERATED"),
                         ("Movie", "MOVIE"), ("Sequence", "SEQUENCE"), ("Tiled", "TILED")):
        material(name, source=source)
    value = material("Missing")
    value.node_tree.nodes["Renamed Image"].image = None
    value = material("Projection")
    value.node_tree.nodes["Renamed Image"].projection = "BOX"
    value = material("MutedImage")
    value.node_tree.nodes["Renamed Image"].mute = True
    value = material("MappedVector")
    mapping = value.node_tree.nodes.new("ShaderNodeMapping")
    value.node_tree.links.new(mapping.outputs["Vector"],
                             value.node_tree.nodes["Renamed Image"].inputs["Vector"])
    value = material("ColorScalar", scalar="Roughness")
    texture = value.node_tree.nodes["Renamed Image"]
    surface = value.node_tree.nodes["Renamed Surface"]
    value.node_tree.links.new(texture.outputs["Color"], surface.inputs["Roughness"])
    bpy.context.view_layer.update()


def oracle():
    if bpy.context.scene.name != "Textures" or len(bpy.data.scenes) != 1:
        raise RuntimeError("Texture fixture must retain one active Textures scene")
    materials = {}
    wraps = {"REPEAT": "repeat", "EXTEND": "clamp", "CLIP": "black", "MIRROR": "mirror"}
    for value in sorted(bpy.data.materials, key=lambda item: item.name):
        texture = value.node_tree.nodes["Renamed Image"]
        surface = value.node_tree.nodes["Renamed Surface"]
        normal = next((node for node in value.node_tree.nodes if node.type == "NORMAL_MAP"), None)
        image = texture.image
        vector = list(texture.inputs["Vector"].links)
        uv = vector[0].from_node.uv_map if vector and vector[0].from_node.type == "UVMAP" else ""
        scalar = next((name for name in ("Metallic", "Roughness", "IOR", "Coat Weight", "Coat Roughness")
                       if surface.inputs[name].is_linked), None)
        input_name = "Normal" if normal else scalar or "Base Color"
        link = surface.inputs[input_name].links[0]
        diagnostics = []
        supported = True
        if texture.mute or texture.projection != "FLAT" or (
            vector and vector[0].from_node.type != "UVMAP"
        ) or (scalar is not None and link.from_socket.name != "Alpha"):
            diagnostics.append("BLEND_MATERIAL_UNSUPPORTED_NODE")
            supported = False
        if normal and (normal.space != "TANGENT" or normal.convention != "OPENGL" or normal.base != "DISPLACED"
                       or normal.inputs["Strength"].default_value != 1):
            diagnostics.append("BLEND_MATERIAL_UNSUPPORTED_NODE")
            supported = False
        if supported:
            if image is None:
                diagnostics.append("BLEND_IMAGE_MISSING")
                supported = False
            elif image.packed_file:
                diagnostics.append("BLEND_IMAGE_PACKED")
                supported = False
            elif image.source != "FILE":
                diagnostics.append("BLEND_IMAGE_SOURCE_UNSUPPORTED")
                supported = False
            else:
                if texture.interpolation != "Linear":
                    diagnostics.append("BLEND_IMAGE_INTERPOLATION_UNSUPPORTED")
                if not image.filepath.startswith("//"):
                    diagnostics.append("BLEND_IMAGE_ABSOLUTE_PATH")
        record = {
            "diffuseColor": list(surface.inputs["Base Color"].default_value[:3]),
            "roughness": surface.inputs["Roughness"].default_value,
            "diagnostics": sorted(diagnostics),
            "texture": None,
        }
        if supported:
            path = image.filepath
            asset_path = "./" + path[2:] if path.startswith("//") else path
            record["texture"] = {
                "input": input_name, "output": "rgb" if normal or input_name == "Base Color" else "a",
                "assetPath": asset_path.replace("\\", "/"),
                "sourceColorSpace": "raw" if image.colorspace_settings.name == "Non-Color" else "sRGB",
                "wrap": wraps[texture.extension], "uvMap": uv,
                "varname": {"": "st", "Render": "st", "Detail_UV": "Detail_UV_1"}[uv],
                "normal": normal is not None,
            }
        materials[value.name] = record
    return {"version": bpy.app.version_string, "scene": bpy.context.scene.name,
            "materials": materials}


def compare(expected):
    if expected != oracle():
        raise RuntimeError("Texture oracle differs")


def save(output):
    make_textures(output)
    bpy.ops.wm.save_as_mainfile(filepath=str(output), compress=False,
                              check_existing=False, relative_remap=False)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, default=ROOT / "textures.blend")
    parser.add_argument("--check", action="store_true")
    arguments = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    options = parser.parse_args(arguments)
    if bpy.app.version != (5, 2, 2):
        raise RuntimeError(f"Unsupported fixture Blender version: {bpy.app.version_string}")
    output = options.output.resolve()
    oracle_path = output.with_suffix(".oracle.json")
    if options.check:
        expected = json.loads(oracle_path.read_text(encoding="ascii"))
        with TemporaryDirectory(prefix="blend-textures-") as directory:
            save(Path(directory) / "textures.blend")
            compare(expected)
    else:
        output.parent.mkdir(parents=True, exist_ok=True)
        save(output)
        expected = oracle()
        oracle_path.write_text(json.dumps(expected, indent=2, sort_keys=True) + "\n", encoding="ascii")
    bpy.ops.wm.open_mainfile(filepath=str(output), load_ui=False, use_scripts=False)
    compare(expected)
    print(f"Verified saved texture oracle with Blender {bpy.app.version_string}: {output}")


if __name__ == "__main__":
    main()

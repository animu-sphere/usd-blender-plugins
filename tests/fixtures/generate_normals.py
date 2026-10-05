import argparse
import math
import sys
from pathlib import Path
from tempfile import TemporaryDirectory

import bpy


VERSIONS = {(3, 3, 21), (4, 5, 13), (5, 2, 2)}
ROOT = Path(__file__).resolve().parent / "native-normals"
GROUPS = ("smooth", "flat", "split", "custom", "custom_fans", "custom_split_fans", "custom_angles", "multi", "constant")
POLYGON_GROUPS = ("polygon_smooth", "polygon_split", "custom_polygon_smooth", "custom_polygon_split")
LEGACY_ANGLES = {"auto_smooth": math.pi, "auto_angle": math.pi / 3,
                 "auto_zero": 0.0, "auto_boundary": math.pi / 2}


def add_mesh(name, points, faces, flat=(), sharp=()):
    mesh = bpy.data.meshes.new(name)
    mesh.from_pydata(points, [], faces)
    for polygon in mesh.polygons:
        polygon.use_smooth = polygon.index not in flat
    for edge in mesh.edges:
        edge.use_edge_sharp = tuple(sorted(edge.vertices)) in sharp
    obj = bpy.data.objects.new(name, mesh)
    bpy.context.scene.collection.objects.link(obj)


def set_custom_normals(mesh, directions):
    normals = [
        tuple(value / math.hypot(*direction) for value in direction) if any(direction) else direction
        for direction in (directions[index % len(directions)] for index in range(len(mesh.loops)))
    ]
    mesh.normals_split_custom_set(normals)


def make_polygon_mesh(group):
    points, faces = [], []

    def add(vertices, polygons):
        offset = len(points)
        points.extend(vertices)
        faces.extend(tuple(vertex + offset for vertex in face) for face in polygons)

    add(((0, 0, 0), (2, 0, 0), (2, 1, 0), (1, 1, 0), (1, 2, 0), (0, 2, 0), (1, 2, 1)),
        ((0, 1, 2, 3, 4, 5), (4, 3, 6)))
    add(((0, 0, 0), (3, 0, 1), (3, 2, -0.5), (0, 2, 0), (0, -1, 2)),
        ((0, 1, 2, 3), (1, 0, 4)))
    add(((0, 0, 0), (3, 0, 0.5), (1, 1, -0.5), (3, 3, 1), (0, 3, 0), (0, -2, 1)),
        ((0, 1, 2, 3, 4), (1, 0, 5)))
    add(((0, 0, 0), (2, 0, 0), (0, 3, 0), (1, -2, 4), (-2, 1, 2)),
        ((0, 1, 2), (0, 3, 4)))
    add(((0, 0, 0), (2, 0, 0), (2, 3, 0), (0, 3, 3)),
        ((0, 1, 2), (0, 2, 3)))
    for degrees in range(5, 180, 5):
        angle = math.radians(degrees)
        add(((0, 0, 0), (2, 0, 0), (0, 3, 0),
             (math.cos(angle), -0.6 * math.sin(angle), 0.8 * math.sin(angle))),
            ((0, 1, 2), (1, 0, 3)))
    flat = ()
    if group.endswith("_split"):
        flat = (len(faces),)
        add(((0, 0, 0), (1, 0, 0), (0, 1, 0)), ((0, 1, 2),))
    sharp = ((3, 4), (6, 7)) if group == "custom_polygon_split" else ()
    add_mesh(group.capitalize(), points, faces, flat, sharp)
    if group.startswith("custom"):
        mesh = bpy.data.objects[group.capitalize()].data
        set_custom_normals(mesh, ((1, 2, 3),))
        pairs = ((0, 0), (16384, 32767), (-16384, -16384),
                 (-32768, -32768), (10000, 5000), (-10001, -7001))
        for corner, element in enumerate(mesh.attributes["custom_normal"].data):
            element.value = pairs[corner % len(pairs)]
        mesh.update()


def make_scene(group):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    for screen in bpy.data.screens:
        for area in screen.areas:
            for space in area.spaces:
                if space.type == "FILE_BROWSER" and space.params is not None:
                    space.params.directory = b"//"
    bpy.context.preferences.filepaths.save_version = 0
    bpy.context.scene.name = "Normals"
    bpy.context.scene.unit_settings.scale_length = 0.01
    if group in POLYGON_GROUPS:
        make_polygon_mesh(group)
        bpy.context.view_layer.update()
        return
    points, polygons, flat_faces, sharp_edges = [], [], [], []
    geometry_group = "flat" if group == "constant" else "split" if group in ("custom", "custom_split_fans") else "smooth" if group in ("multi", "custom_fans") else group

    def add_case(name, vertices, faces, flat=(), sharp=()):
        category = "flat" if len(flat) == len(faces) else "split" if flat or sharp else "smooth"
        if group not in LEGACY_ANGLES and category != geometry_group:
            return
        vertex_offset, face_offset = len(points), len(polygons)
        points.extend(vertices)
        polygons.extend(tuple(vertex + vertex_offset for vertex in face) for face in faces)
        flat_faces.extend(face + face_offset for face in flat)
        sharp_edges.extend(tuple(vertex + vertex_offset for vertex in edge) for edge in sharp)

    wedge = ((0, 0, 0), (2, 0, 0), (0, 3, 0), (1, -2, 4))
    faces = ((0, 1, 2), (1, 0, 3))
    add_case("SmoothWedge", wedge, faces)
    add_case("SharpWedge", wedge, faces, sharp=((0, 1),))
    add_case("MixedWedge", wedge, faces, flat=(0,))
    add_case("FlatWedge", wedge, faces, flat=(0, 1))
    add_case("SameDirection", wedge, ((0, 1, 2), (0, 1, 3)))
    add_case("SplitSameDirection", wedge, ((0, 1, 2), (0, 1, 3)), sharp=((1, 2),))
    add_case("NonManifold", wedge + ((0, -3, -1),), faces + ((0, 1, 4),))
    add_case("SplitNonManifold", wedge + ((0, -3, -1),), faces + ((0, 1, 4),),
             sharp=((1, 2),))
    add_case("Disconnected", wedge + ((-2, 1, 2),), ((0, 1, 2), (0, 3, 4)))
    add_case("SplitDisconnected", wedge + ((-2, 1, 2),), ((0, 1, 2), (0, 3, 4)),
             sharp=((0, 1),))
    fan = ((0, 0, 0), (2, 0, 0), (1, 2, 1), (-1, 1, 2), (-2, -1, 0))
    fan_faces = ((0, 1, 2), (0, 2, 3), (0, 3, 4))
    add_case("BoundaryFan", fan, fan_faces)
    add_case("SplitFan", fan, fan_faces, sharp=((0, 3),))
    cube = ((-1, -2, -3), (1, -2, -3), (1, 2, -3), (-1, 2, -3),
            (-1, -2, 3), (1, -2, 3), (1, 2, 3), (-1, 2, 3))
    cube_faces = ((3, 2, 1, 0), (4, 5, 6, 7), (0, 1, 5, 4),
                  (1, 2, 6, 5), (2, 3, 7, 6), (3, 0, 4, 7))
    add_case("SmoothCube", cube, cube_faces)
    add_case("MixedCube", cube, cube_faces, flat=(0, 2), sharp=((2, 6), (3, 7)))
    add_case("Concave", ((0, 0, 0), (3, 0, 0), (1, 1, 0), (3, 3, 0), (0, 3, 0)),
             ((0, 1, 2, 3, 4),))
    if group in LEGACY_ANGLES:
        for height in (-0.0001, 0.0, 0.0001):
            add_case("RightAngle", ((0, 0, 0), (2, 0, 0), (0, 3, 0), (0, height, 3)), faces)
        add_case("Coplanar", ((0, 0, 0), (2, 0, 0), (0, 3, 0), (0, -3, 0)), faces)
    if group == "custom_angles":
        for index in range(-99, 100):
            x = index / 100
            start = len(points)
            points.extend(((0, 0, 0), (1, 0, 0), (x, math.sqrt(1 - x * x), 0)))
            polygons.append((start, start + 1, start + 2))
    add_mesh(group.capitalize(), points, polygons, flat_faces, sharp_edges)
    if group in LEGACY_ANGLES:
        mesh = bpy.data.objects[group.capitalize()].data
        mesh.use_auto_smooth = True
        mesh.auto_smooth_angle = LEGACY_ANGLES[group]
    elif group.startswith("custom"):
        mesh = bpy.data.objects[group.capitalize()].data
        directions = ((0, 0, 0), (1, 2, 3), (-2, 1, -3), (0, 0, -1)) if group == "custom" else ((1, 2, 3),)
        set_custom_normals(mesh, directions)
        if group == "custom_angles":
            for element in mesh.attributes["custom_normal"].data:
                element.value = (16384, 32767)
            mesh.update()
        elif group == "custom_split_fans":
            values = mesh.attributes["custom_normal"].data
            for corner, pair in {
                2: (-32768, -32768),
                46: (10000, 5000), 64: (18511, 6420),
                47: (-10001, -7001), 57: (-18512, -6420),
                61: (0, 0), 48: (10000, 20000), 56: (20000, 0),
            }.items():
                values[corner].value = pair
            mesh.update()
    elif group in ("multi", "constant"):
        other_points = [(x * 2 + 7, y * 2 - 5, z * 2 + 2) for x, y, z in points]
        add_mesh("Other", other_points, polygons, flat_faces, sharp_edges)
        if group == "constant":
            for obj in bpy.context.scene.objects:
                attribute = obj.data.attributes.get("sharp_face")
                if attribute is not None:
                    obj.data.attributes.remove(attribute)
                obj.select_set(True)
            bpy.context.view_layer.objects.active = bpy.data.objects["Constant"]
            bpy.ops.object.shade_flat()
    bpy.context.view_layer.update()


def oracle_text(group):
    scene = bpy.context.scene
    if scene.name != "Normals" or len(bpy.data.scenes) != 1:
        raise RuntimeError("Normal fixture must contain one active Scene named Normals")
    objects = sorted(scene.objects, key=lambda obj: obj.name)
    expected_names = [group.capitalize(), "Other"] if group in ("multi", "constant") else [group.capitalize()]
    if [obj.name for obj in objects] != expected_names:
        raise RuntimeError("Normal fixture objects differ from the selected group")
    custom = group.startswith("custom")
    legacy = group in LEGACY_ANGLES
    rows = [f"BLEND_NORMALS_ORACLE {3 if legacy else 2 if custom else 1}", repr(bpy.app.version_string),
            f"{scene.unit_settings.scale_length:.17g} {len(objects)}"]
    for obj in objects:
        if obj.type != "MESH" or obj.modifiers or obj.data.has_custom_normals != custom:
            raise RuntimeError("Normal fixture requires source meshes with the selected custom-normal state")
        mesh = obj.data
        rows.append(f'"{obj.name}" {len(mesh.vertices)} {len(mesh.polygons)} {len(mesh.loops)}')
        if legacy:
            rows.append(f"AUTO_SMOOTH {int(mesh.use_auto_smooth)} {mesh.auto_smooth_angle:.17g}")
            mesh.calc_normals_split()
        rows.extend(" ".join(f"{value:.17g}" for value in vertex.co) for vertex in mesh.vertices)
        rows.append(" ".join(str(polygon.loop_total) for polygon in mesh.polygons))
        rows.append(" ".join(str(loop.vertex_index) for loop in mesh.loops))
        normals = (loop.normal for loop in mesh.loops) if legacy else (normal.vector for normal in mesh.corner_normals)
        rows.extend(" ".join(f"{value:.17g}" for value in normal) for normal in normals)
        if custom:
            attribute = mesh.attributes["custom_normal"]
            if attribute.data_type != "INT16_2D" or attribute.domain != "CORNER":
                raise RuntimeError("Custom normal fixture requires packed corner short pairs")
            rows.append(f"PACKED_CUSTOM_NORMALS {len(attribute.data)}")
            rows.extend(" ".join(str(value) for value in element.value) for element in attribute.data)
    return "\n".join(rows) + "\n"


def compare_oracle(expected, actual):
    left = expected.splitlines()
    right = actual.splitlines()
    if len(left) != len(right):
        raise RuntimeError("Normal oracle record count differs")
    for index, (reference, value) in enumerate(zip(left, right)):
        if index < 3 or reference.startswith(('"', "PACKED_CUSTOM_NORMALS ", "AUTO_SMOOTH ")):
            if reference != value:
                raise RuntimeError(f"Normal oracle metadata differs at line {index + 1}")
        else:
            expected_values = list(map(float, reference.split()))
            actual_values = list(map(float, value.split()))
            if len(expected_values) != len(actual_values) or not all(
                math.isfinite(number) and math.isclose(number, target, rel_tol=2e-6, abs_tol=2e-6)
                for target, number in zip(expected_values, actual_values)
            ):
                raise RuntimeError(f"Normal oracle values differ at line {index + 1}")


def save_scene(output, group):
    make_scene(group)
    output.parent.mkdir(parents=True, exist_ok=True)
    bpy.ops.wm.save_as_mainfile(filepath=str(output), compress=False, check_existing=False)


def main():
    parser = argparse.ArgumentParser()
    version = ".".join(map(str, bpy.app.version))
    parser.add_argument("--output", type=Path, default=ROOT / f"blender-{version}")
    parser.add_argument("--check", action="store_true")
    groups = tuple(LEGACY_ANGLES) if bpy.app.version == (3, 3, 21) else GROUPS
    if bpy.app.version >= (5, 0, 0):
        groups += POLYGON_GROUPS
    parser.add_argument("--groups", nargs="+", choices=groups, default=groups)
    arguments = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    options = parser.parse_args(arguments)
    if bpy.app.version not in VERSIONS:
        raise RuntimeError(f"Unsupported fixture Blender version: {bpy.app.version_string}")
    for group in options.groups:
        output = options.output.resolve() / f"{group}.blend"
        oracle = output.with_suffix(".oracle.txt")
        if options.check:
            expected = oracle.read_text(encoding="ascii")
            with TemporaryDirectory(prefix="blend-normals-") as directory:
                save_scene(Path(directory) / f"{group}.blend", group)
                compare_oracle(expected, oracle_text(group))
        else:
            save_scene(output, group)
            expected = oracle_text(group)
            oracle.write_text(expected, encoding="ascii")
        bpy.ops.wm.open_mainfile(filepath=str(output), load_ui=False, use_scripts=False)
        compare_oracle(expected, oracle_text(group))
        print(f"Verified saved normal oracle with Blender {bpy.app.version_string}: {output}")


if __name__ == "__main__":
    main()

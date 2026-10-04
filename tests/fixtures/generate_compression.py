import argparse
import gzip
import hashlib
import json
import re
import shutil
import subprocess
import sys
from pathlib import Path

import bpy
import numpy as np


VERSIONS = {(4, 5, 13), (5, 2, 2)}
PROFILES = {
    "small": {"triangles": 32, "points": 64, "image_side": 16, "images": 2},
    "policy": {
        "triangles": 524288, "points": 2097152, "image_side": 2048, "images": 4,
    },
}
CASES = ("large_mesh", "repetitive", "packed_assets")
EVIDENCE = re.compile(
    r"Compression evidence: [^\r\n]+ input=(\d+) output=(\d+) "
    r"integer-ratio=(\d+) minimum-accepted-window-log=(\d+)"
)


def make_scene(case, profile):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene = bpy.context.scene
    scene.name = "CompressionPolicy"
    rng = np.random.default_rng(20261005)
    mesh = bpy.data.meshes.new(case)
    obj = bpy.data.objects.new(case, mesh)
    scene.collection.objects.link(obj)
    if case == "large_mesh":
        count = profile["triangles"]
        mesh.vertices.add(count * 3)
        mesh.vertices.foreach_set(
            "co", rng.uniform(-100, 100, count * 9).astype(np.float32)
        )
        mesh.loops.add(count * 3)
        mesh.loops.foreach_set("vertex_index", np.arange(count * 3, dtype=np.int32))
        mesh.polygons.add(count)
        mesh.polygons.foreach_set("loop_start", np.arange(count, dtype=np.int32) * 3)
        mesh.polygons.foreach_set("loop_total", np.full(count, 3, dtype=np.int32))
        mesh.update(calc_edges=True)
    elif case == "repetitive":
        count = profile["points"]
        mesh.vertices.add(count)
        mesh.vertices.foreach_set("co", np.zeros(count * 3, dtype=np.float32))
        for index in range(8):
            attribute = mesh.attributes.new(f"constant_{index}", "FLOAT", "POINT")
            attribute.data.foreach_set("value", np.zeros(count, dtype=np.float32))
        mesh.update()
    elif case == "packed_assets":
        mesh.from_pydata([(0, 0, 0), (1, 0, 0), (0, 1, 0)], [], [(0, 1, 2)])
        material = bpy.data.materials.new("PackedNoise")
        material.use_nodes = True
        mesh.materials.append(material)
        side = profile["image_side"]
        for index in range(profile["images"]):
            image = bpy.data.images.new(f"noise_{index}", side, side, alpha=True)
            pixels = rng.integers(0, 256, side * side * 4, dtype=np.uint32)
            image.pixels.foreach_set(pixels.astype(np.float32) / 255)
            image.pack()
            node = material.node_tree.nodes.new("ShaderNodeTexImage")
            node.image = image
    else:
        raise ValueError(f"Unknown compression case: {case}")
    bpy.context.view_layer.update()
    return scene


def validate_scene(case, profile):
    mesh = bpy.data.meshes[case]
    if case == "large_mesh":
        expected = profile["triangles"]
        if len(mesh.vertices) != expected * 3 or len(mesh.polygons) != expected:
            raise RuntimeError("Saved large-mesh counts differ")
    elif case == "repetitive":
        expected = profile["points"]
        if len(mesh.vertices) != expected:
            raise RuntimeError("Saved repetitive point count differs")
        values = np.empty(expected, dtype=np.float32)
        for index in range(8):
            mesh.attributes[f"constant_{index}"].data.foreach_get("value", values)
            if np.any(values != 0):
                raise RuntimeError("Saved repetitive attributes differ")
    elif case == "packed_assets":
        side = profile["image_side"]
        for index in range(profile["images"]):
            image = bpy.data.images[f"noise_{index}"]
            if tuple(image.size) != (side, side) or image.packed_file is None:
                raise RuntimeError("Saved packed image differs")
    else:
        raise ValueError(f"Unknown compression case: {case}")


def file_record(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return {
        "path": path.name, "stored_bytes": path.stat().st_size,
        "sha256": digest.hexdigest(),
    }


def generate_case(directory, case, profile, reader):
    scene = make_scene(case, profile)
    raw = directory / f"{case}.raw.blend"
    compressed = directory / f"{case}.blend"
    wrapped = directory / f"{case}.gzip.blend"
    bpy.data.libraries.write(str(raw), {scene}, fake_user=True, compress=False)
    bpy.data.libraries.write(str(compressed), {scene}, fake_user=True, compress=True)
    with raw.open("rb") as source, wrapped.open("wb") as destination:
        with gzip.GzipFile(
            filename="", mode="wb", fileobj=destination, compresslevel=9, mtime=0,
        ) as encoder:
            shutil.copyfileobj(source, encoder)
    for path in (raw, compressed):
        bpy.ops.wm.open_mainfile(filepath=str(path), load_ui=False, use_scripts=False)
        validate_scene(case, profile)
    with compressed.open("rb") as stream:
        magic = stream.read(4)
    if magic != b"\x28\xb5\x2f\xfd":
        raise RuntimeError(f"Expected Blender-written Zstandard, got {magic!r}")
    result = subprocess.run(
        [str(reader), "--measure-compression", str(compressed), str(wrapped)],
        capture_output=True, text=True, timeout=600,
    )
    if result.returncode:
        raise RuntimeError(f"Reader measurement failed:\n{result.stdout}{result.stderr}")
    matches = EVIDENCE.findall(result.stdout)
    if len(matches) != 2:
        raise RuntimeError(f"Reader evidence has an unexpected shape:\n{result.stdout}")
    records = []
    for path, codec, values in zip((compressed, wrapped), ("zstd", "gzip"), matches):
        record = file_record(path)
        stored, decoded, ratio, window = map(int, values)
        if stored != record["stored_bytes"] or (
            codec == "gzip" and decoded != raw.stat().st_size
        ):
            raise RuntimeError("Reader sizes differ from saved files")
        record.update({
            "codec": codec, "decoded_bytes": decoded,
            "minimum_integer_ratio": ratio, "minimum_window_log": window,
            "blender_written_compression": codec == "zstd",
        })
        records.append(record)
    print(result.stdout, end="")
    return {"case": case, "raw": file_record(raw), "compressed": records}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--reader", required=True, type=Path)
    parser.add_argument("--profile", choices=PROFILES, default="policy")
    parser.add_argument("--case", choices=CASES, action="append")
    arguments = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    options = parser.parse_args(arguments)
    if bpy.app.version not in VERSIONS:
        raise RuntimeError(f"Unsupported measurement Blender version: {bpy.app.version_string}")
    reader = options.reader.resolve()
    if not reader.is_file():
        raise RuntimeError(f"Reader executable is missing: {reader}")
    cases = options.case or CASES
    if len(set(cases)) != len(cases):
        parser.error("Each --case may be supplied only once")
    directory = options.output.resolve()
    directory.mkdir(parents=True, exist_ok=True)
    outputs = [directory / f"{case}{suffix}" for case in cases
               for suffix in (".raw.blend", ".blend", ".gzip.blend")]
    manifest = directory / "manifest.json"
    if manifest.exists() or any(path.exists() for path in outputs):
        raise RuntimeError("Measurement output already exists; choose a fresh directory")
    evidence = {
        "blender_version": bpy.app.version_string,
        "blender_version_tuple": list(bpy.app.version),
        "blender_build_hash": bpy.app.build_hash.decode("ascii"),
        "numpy_version": np.__version__,
        "profile": options.profile, "parameters": PROFILES[options.profile],
        "seed": 20261005,
        "cases": [generate_case(directory, case, PROFILES[options.profile], reader)
                  for case in cases],
    }
    manifest.write_text(json.dumps(evidence, indent=2) + "\n", encoding="ascii")
    print(f"Recorded compression measurements: {manifest}")


if __name__ == "__main__":
    main()

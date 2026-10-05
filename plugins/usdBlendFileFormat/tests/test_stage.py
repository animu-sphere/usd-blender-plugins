import gzip
import math
import shlex
import struct
import subprocess
import sys
import unittest
from pathlib import Path
from tempfile import TemporaryDirectory

from pxr import Sdf, Tf, Usd, UsdGeom


FIXTURES = Path(__file__).resolve().parent / "fixtures"
NATIVE = Path(__file__).resolve().parents[3] / "tests" / "fixtures"
SCENES = [NATIVE / "native-scene" / version / "scene.blend" for version in ("blender-4.5.13", "blender-5.2.2")]
FALLBACKS = [NATIVE / "native-scene" / "blender-5.2.2" / "fallbacks.blend"]
EVALUATION = [NATIVE / "native-scene" / "blender-5.2.2" / "evaluation.blend"]
MESHES = [NATIVE / "native-mesh" / version / "mesh.blend"
          for version in ("blender-3.3.21", "blender-4.5.13", "blender-5.2.2")]
LEGACY_NORMALS = [NATIVE / "native-normals" / "blender-3.3.21" / f"{group}.blend"
                  for group in ("auto_smooth", "auto_angle", "auto_zero", "auto_boundary")]
POLYGON_NORMALS = [NATIVE / "native-normals" / "blender-5.2.2" / f"{group}.blend"
                   for group in ("polygon_smooth", "polygon_split")]
COMPRESSED_CORPUS = FIXTURES.parent / "corpus" / "blender-5.2.2" / "Untitled.blend"


def raw_zstd_frame(data):
    frame = bytearray(b"\x28\xb5\x2f\xfd\xa0" + len(data).to_bytes(4, "little"))
    for offset in range(0, len(data), 128 * 1024):
        block = data[offset:offset + 128 * 1024]
        last = offset + len(block) == len(data)
        frame.extend(((len(block) << 3) | last).to_bytes(3, "little"))
        frame.extend(block)
    return bytes(frame)


def converted_vector(value, scale=1.0):
    return (value[0] * scale, value[2] * scale, -value[1] * scale)


def converted_matrix(values, scale):
    if len(values) != 16 or not all(math.isfinite(value) for value in values):
        raise ValueError("Oracle matrices require 16 finite values")
    if any(abs(value - expected) > 1e-6 for value, expected in zip(values[12:], (0, 0, 0, 1))):
        raise ValueError("Oracle matrix must be affine before canonicalization")
    source = [values[index:index + 4] for index in range(0, 16, 4)]
    for row in range(3):
        source[row][3] *= scale
    source[3] = [0.0, 0.0, 0.0, 1.0]
    axes, signs = (0, 2, 1, 3), (1, 1, -1, 1)
    return [
        [signs[row] * signs[column] * source[axes[row]][axes[column]] for row in range(4)]
        for column in range(4)
    ]


def cube_blocks(data):
    offset = 17
    while offset < len(data):
        code, _, _, length, _ = struct.unpack_from("<4siQqq", data, offset)
        yield code, offset, offset + 32, length
        offset += 32 + length


class StageContractTests(unittest.TestCase):
    def _assert_contract(self, stage, source_version, source_scene):
        self.assertIsNotNone(stage)
        self.assertEqual(str(stage.GetDefaultPrim().GetPath()), "/Asset")
        self.assertEqual(stage.GetDefaultPrim().GetTypeName(), "Xform")
        self.assertEqual(stage.GetDefaultPrim().GetMetadata("kind"), "component")
        self.assertEqual(stage.GetDefaultPrim().GetCustomDataByKey("blend:stageContractVersion"), 1)
        self.assertEqual(stage.GetDefaultPrim().GetCustomDataByKey("blend:sourceVersion"), source_version)
        self.assertEqual(stage.GetDefaultPrim().GetCustomDataByKey("blend:sourceScene"), source_scene)
        self.assertEqual(UsdGeom.GetStageUpAxis(stage), "Y")
        self.assertEqual(UsdGeom.GetStageMetersPerUnit(stage), 1.0)
        self.assertEqual(stage.GetPrimAtPath("/Asset/geo").GetTypeName(), "Scope")
        self.assertEqual(stage.GetPrimAtPath("/Asset/mtl").GetTypeName(), "Scope")
        self.assertEqual(stage.GetRootLayer().customLayerData, {})

    def _assert_close(self, actual, expected, tolerance=2e-5):
        self.assertEqual(len(actual), len(expected))
        for value, target in zip(actual, expected):
            if isinstance(target, (list, tuple)):
                self._assert_close(value, target, tolerance)
            else:
                self.assertLessEqual(abs(value - target), tolerance * (1 + abs(target)))

    def test_single_cube(self):
        stage = Usd.Stage.Open(str(FIXTURES / "single_cube.blend"))
        self._assert_contract(stage, "5.2", "Scene")
        self.assertEqual(
            [(str(prim.GetPath()), prim.GetTypeName()) for prim in stage.Traverse()],
            [("/Asset", "Xform"), ("/Asset/geo", "Scope"), ("/Asset/geo/Cube", "Xform"),
             ("/Asset/geo/Cube/mesh", "Mesh"), ("/Asset/mtl", "Scope")],
        )
        mesh = UsdGeom.Mesh(stage.GetPrimAtPath("/Asset/geo/Cube/mesh"))
        expected = {(x, y, z) for x in (-1.0, 1.0) for y in (-1.0, 1.0) for z in (-1.0, 1.0)}
        self.assertEqual({tuple(point) for point in mesh.GetPointsAttr().Get()}, expected)
        self.assertEqual(list(mesh.GetFaceVertexCountsAttr().Get()), [4] * 6)
        self.assertEqual(len(mesh.GetFaceVertexIndicesAttr().Get()), 24)
        self.assertEqual(len(mesh.GetNormalsAttr().Get()), 24)
        self.assertEqual(mesh.GetNormalsInterpolation(), "faceVarying")
        self.assertEqual(mesh.GetOrientationAttr().Get(), "rightHanded")
        self.assertEqual(mesh.GetSubdivisionSchemeAttr().Get(), "none")
        self.assertEqual([tuple(value) for value in mesh.GetExtentAttr().Get()], [(-1, -1, -1), (1, 1, 1)])
        uv = UsdGeom.PrimvarsAPI(mesh).GetPrimvar("st")
        self.assertEqual(uv.GetTypeName(), Sdf.ValueTypeNames.TexCoord2fArray)
        self.assertEqual(len(uv.ComputeFlattened()), 24)

    def test_input_failures(self):
        cases = {
            "invalid.blend": "BLEND_HEADER_MAGIC",
            "truncated.blend": "BLEND_HEADER_TRUNCATED",
            "pointer_size.blend": "BLEND_HEADER_POINTER_SIZE",
            "endianness.blend": "BLEND_HEADER_ENDIANNESS",
            "version.blend": "BLEND_HEADER_VERSION",
            "header_only.blend": "BLEND_BLOCK_MISSING_ENDB",
            "empty.blend": "BLEND_SCENE_ACTIVE_MISSING",
        }
        for fixture, code in cases.items():
            with self.subTest(fixture=fixture):
                with self.assertRaises(Tf.ErrorException) as error:
                    Usd.Stage.Open(str(FIXTURES / fixture))
                self.assertIn(code, str(error.exception))
        corpus = FIXTURES.parent / "corpus" / "blender-4.5.13" / "Untitled.blend"
        stage = Usd.Stage.Open(str(corpus))
        self._assert_contract(stage, "4.5", "Scene")
        for name in ("Camera", "Light"):
            prim = stage.GetPrimAtPath(f"/Asset/geo/{name}")
            self.assertEqual(prim.GetTypeName(), "Xform")
            self.assertEqual(prim.GetChildren(), [])
        self.assertTrue(stage.GetPrimAtPath("/Asset/geo/Cube/mesh").IsA(UsdGeom.Mesh))

    def test_container_failures_and_compression(self):
        data = (FIXTURES / "single_cube.blend").read_bytes()
        blocks = list(cube_blocks(data))
        _, dna_start, dna_payload, dna_length = next(block for block in blocks if block[0] == b"DNA1")
        _, end_start, _, _ = blocks[-1]
        missing = bytearray(data)
        missing[dna_start:dna_start + 4] = b"TEST"
        malformed = bytearray(data)
        malformed[dna_payload] = 0
        cases = [
            (data[:-1], "BLEND_BLOCK_TRUNCATED"),
            (data + b"x", "BLEND_BLOCK_TRAILING"),
            (missing, "BLEND_DNA_BLOCK"),
            (data[:end_start] + data[dna_start:dna_payload + dna_length] + data[end_start:], "BLEND_DNA_BLOCK"),
            (malformed, "BLEND_DNA_SECTION"),
        ]
        gzip_data = gzip.compress(data, mtime=0)
        bad_crc = bytearray(gzip_data)
        bad_crc[-8] ^= 1
        zstd_data = raw_zstd_frame(data)
        compressed = COMPRESSED_CORPUS.read_bytes()
        cases.extend([
            (gzip_data[:-1], "BLEND_COMPRESSION_TRUNCATED"),
            (bad_crc, "BLEND_COMPRESSION_INVALID"),
            (gzip_data + b"not-a-member", "BLEND_COMPRESSION_INVALID"),
            (zstd_data[:-1], "BLEND_COMPRESSION_TRUNCATED"),
            (zstd_data + b"x", "BLEND_COMPRESSION_INVALID"),
            (compressed[:-1], "BLEND_COMPRESSION_TRUNCATED"),
            (compressed + b"x", "BLEND_COMPRESSION_INVALID"),
            (b"\x28\xb5\x2f\xfd\x00\x70" + zstd_data[9:], "BLEND_COMPRESSION_WINDOW_LIMIT"),
            (b"\x28\xb5\x2f\xfd\xa0" + (262144).to_bytes(4, "little") +
             (131072 << 3 | 2).to_bytes(3, "little") + b"\x00" +
             (131072 << 3 | 3).to_bytes(3, "little") + b"\x00", "BLEND_COMPRESSION_RATIO_LIMIT"),
        ])
        cases.extend((encode(payload), code) for payload, code in list(cases[:5])
                     for encode in (lambda payload: gzip.compress(payload, mtime=0), raw_zstd_frame))
        with TemporaryDirectory(prefix="blend-import-") as directory:
            for index, (payload, code) in enumerate(cases):
                path = Path(directory) / f"invalid-{index}.blend"
                path.write_bytes(payload)
                for metadata_only in (False, True):
                    with self.subTest(code=code, metadata_only=metadata_only):
                        with self.assertRaises(Tf.ErrorException) as error:
                            Sdf.Layer.OpenAsAnonymous(str(path), metadataOnly=metadata_only)
                        self.assertIn(code, str(error.exception))
                        if code == "BLEND_DNA_SECTION":
                            self.assertIn(f"byte {dna_payload}", str(error.exception))

    def test_compressed_scene_equivalence(self):
        with TemporaryDirectory(prefix="blend-compressed-") as directory:
            for fixture in [FIXTURES / "single_cube.blend", SCENES[1], FALLBACKS[0]]:
                data = fixture.read_bytes()
                encodings = [
                    gzip.compress(data, mtime=0),
                    raw_zstd_frame(data),
                    gzip.compress(data[:5], mtime=0) + gzip.compress(data[5:], mtime=0),
                    raw_zstd_frame(data[:5]) + raw_zstd_frame(data[5:]),
                ]
                end_start = list(cube_blocks(data))[-1][1]
                dense = data[:end_start] + struct.pack("<4siQqq", b"TEST", 0, 0, 0, 0) * 6000 + data[end_start:]
                encodings.append(gzip.compress(dense, mtime=0))
                self.assertGreater(len(list(cube_blocks(dense))), len(encodings[-1]) // 20 + 1)
                for index, payload in enumerate(encodings):
                    path = Path(directory) / f"{fixture.stem}-{index}.blend"
                    path.write_bytes(payload)
                    with self.subTest(fixture=str(fixture), encoding=index):
                        for metadata_only in (False, True):
                            expected = Sdf.Layer.OpenAsAnonymous(str(fixture), metadataOnly=metadata_only)
                            actual = Sdf.Layer.OpenAsAnonymous(str(path), metadataOnly=metadata_only)
                            self.assertEqual(actual.ExportToString(), expected.ExportToString())
                            self.assertEqual(actual.ExportToString(),
                                             Sdf.Layer.OpenAsAnonymous(str(path), metadataOnly=metadata_only).ExportToString())
                        stage = Usd.Stage.CreateInMemory()
                        root = stage.DefinePrim("/Referenced")
                        root.GetReferences().AddReference(str(path))
                        self.assertEqual(root.GetCustomDataByKey("blend:stageContractVersion"), 1)
                        self.assertTrue(any(prim.IsA(UsdGeom.Mesh) for prim in stage.Traverse()))
            stage = Usd.Stage.Open(str(COMPRESSED_CORPUS))
            self._assert_contract(stage, "5.2", "Scene")
            for name in ("Camera", "Light"):
                prim = stage.GetPrimAtPath(f"/Asset/geo/{name}")
                self.assertEqual(prim.GetTypeName(), "Xform")
                self.assertEqual(prim.GetChildren(), [])
            self.assertTrue(stage.GetPrimAtPath("/Asset/geo/Cube/mesh").IsA(UsdGeom.Mesh))

    def test_recoverable_diagnostics(self):
        data = bytearray((FIXTURES / "single_cube.blend").read_bytes())
        _, offset, _, _ = next(block for block in cube_blocks(data) if block[0] == b"TEST")
        data[offset:offset + 4] = b"ZZZZ"
        with TemporaryDirectory(prefix="blend-warning-") as directory:
            path = Path(directory) / "warning.blend"
            path.write_bytes(data)
            result = subprocess.run(
                [sys.executable, "-c",
                 "from pxr import Usd; import sys; "
                 "s=Usd.Stage.Open(sys.argv[1]); assert s.GetPrimAtPath('/Asset/geo/Cube/mesh')",
                 str(path)],
                capture_output=True, text=True, check=False,
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("BLEND_BLOCK_UNKNOWN_CODE", result.stderr)
            self.assertIn("block ", result.stderr)

        for metadata_only in (False, True):
            result = subprocess.run(
                [sys.executable, "-c",
                 "from pxr import Sdf; import sys; "
                 "layer=Sdf.Layer.OpenAsAnonymous(sys.argv[1], metadataOnly=sys.argv[2]=='True'); "
                 "assert layer.GetPrimAtPath('/Asset/geo/Root/CameraFallback')",
                 str(FALLBACKS[0]), str(metadata_only)],
                capture_output=True, text=True, check=False,
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            warnings = [line for line in result.stderr.splitlines()
                        if "BLEND_SCENE_OBJECT_DATA_UNSUPPORTED" in line]
            self.assertEqual(len(warnings), 5, result.stderr)
            for name in ("CameraFallback", "ImageFallback", "LightFallback", "OutsideParent", "TextFallback"):
                self.assertEqual(sum(f"datablock {name}" in line for line in warnings), 1, result.stderr)
            self.assertTrue(all("byte " in line and "block " in line for line in warnings), result.stderr)

        data = EVALUATION[0].read_bytes()
        blocks = list(cube_blocks(data))
        for metadata_only in (False, True):
            result = subprocess.run(
                [sys.executable, "-c",
                 "from pxr import Sdf; import sys; "
                 "layer=Sdf.Layer.OpenAsAnonymous(sys.argv[1], metadataOnly=sys.argv[2]=='True'); "
                 "assert layer.GetPrimAtPath('/Asset/geo/Root/MeshParent/mesh')",
                 str(EVALUATION[0]), str(metadata_only)],
                capture_output=True, text=True, check=False,
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            warnings = [line for line in result.stderr.splitlines() if "EVALUATION_UNAPPLIED" in line]
            self.assertEqual(len(warnings), 5, result.stderr)
            for code, names in (
                ("BLEND_SCENE_EVALUATION_UNAPPLIED", ("EmptyRoot", "MeshParent", "OutsideParent", "Root")),
                ("BLEND_MESH_EVALUATION_UNAPPLIED", ("SharedGeometry",)),
            ):
                for name in names:
                    matches = [line for line in warnings if code in line and f"[datablock {name}]" in line]
                    self.assertEqual(len(matches), 1, result.stderr)
                    kind = b"ME" if code == "BLEND_MESH_EVALUATION_UNAPPLIED" else b"OB"
                    contexts = [(index, payload) for index, (block_code, _, payload, length) in enumerate(blocks)
                                if block_code == kind + b"\0\0" and
                                kind + name.encode("ascii") + b"\0" in data[payload:payload + length]]
                    self.assertEqual(len(contexts), 1, name)
                    index, payload = contexts[0]
                    self.assertIn(f"[byte {payload}]", matches[0])
                    self.assertIn(f"[block {index}]", matches[0])

    def test_repeat_read_and_metadata(self):
        for fixture in [FIXTURES / "single_cube.blend", COMPRESSED_CORPUS, *SCENES, *FALLBACKS, *EVALUATION, *MESHES, *LEGACY_NORMALS, *POLYGON_NORMALS]:
            with self.subTest(fixture=str(fixture)):
                path = str(fixture)
                first = Sdf.Layer.OpenAsAnonymous(path)
                second = Sdf.Layer.OpenAsAnonymous(path)
                metadata = Sdf.Layer.OpenAsAnonymous(path, metadataOnly=True)
                self.assertEqual(first.ExportToString(), second.ExportToString())
                full, partial = Usd.Stage.Open(first), Usd.Stage.Open(metadata)
                self.assertEqual(
                    [(str(p.GetPath()), p.GetTypeName()) for p in full.Traverse()],
                    [(str(p.GetPath()), p.GetTypeName()) for p in partial.Traverse()],
                )
                self.assertEqual(full.GetDefaultPrim().GetCustomData(), partial.GetDefaultPrim().GetCustomData())
                for prim in full.Traverse():
                    other = partial.GetPrimAtPath(prim.GetPath())
                    if prim.IsA(UsdGeom.Mesh):
                        self.assertEqual(other.GetAuthoredAttributes(), [])
                    else:
                        for attr in prim.GetAuthoredAttributes():
                            self.assertEqual(attr.Get(), other.GetAttribute(attr.GetName()).Get())
                self.assertEqual(metadata.ExportToString(),
                                 Sdf.Layer.OpenAsAnonymous(path, metadataOnly=True).ExportToString())

    def test_contract_survives_reference(self):
        for fixture in [FIXTURES / "single_cube.blend", COMPRESSED_CORPUS, *SCENES, *FALLBACKS, *EVALUATION, *MESHES, *LEGACY_NORMALS, *POLYGON_NORMALS]:
            with self.subTest(fixture=str(fixture)):
                stage = Usd.Stage.CreateInMemory()
                root = stage.DefinePrim("/Referenced")
                root.GetReferences().AddReference(str(fixture))
                self.assertEqual(root.GetCustomDataByKey("blend:stageContractVersion"), 1)
                self.assertTrue(stage.GetPrimAtPath("/Referenced/geo"))
                self.assertTrue(stage.GetPrimAtPath("/Referenced/mtl"))
                self.assertTrue(any(prim.IsA(UsdGeom.Mesh) for prim in stage.Traverse()))

    def test_integrated_scene_oracles(self):
        for fixture in [*SCENES, *FALLBACKS, *EVALUATION, *MESHES]:
            with self.subTest(fixture=str(fixture)):
                stage = Usd.Stage.Open(str(fixture))
                self._assert_contract(stage, fixture.parent.name.removeprefix("blender-").rsplit(".", 1)[0],
                                      "Integrated" if fixture in SCENES else
                                      "Fallbacks" if fixture in FALLBACKS else
                                      "SourceOnly" if fixture in EVALUATION else "MeshDomains")
                records = iter(shlex.split(line) for line in fixture.with_suffix(".oracle.txt").read_text().splitlines())
                self.assertEqual(next(records), ["BLEND_SCENE_ORACLE", "1"])
                next(records)
                scale, object_count, mesh_count = next(records)
                scale = float(scale)
                objects = {
                    prim.GetCustomDataByKey("blend:sourceName"): prim for prim in stage.Traverse()
                    if prim.IsA(UsdGeom.Xform) and prim.GetPath() != Sdf.Path("/Asset")
                }
                self.assertEqual(len(objects), int(object_count))
                cache = UsdGeom.XformCache()
                mesh_objects = {}
                for _ in range(int(object_count)):
                    tag, name, parent, mesh_name, hidden = next(records)
                    self.assertEqual(tag, "OBJECT")
                    prim = objects[name]
                    expected_parent = objects[parent].GetPath() if parent in objects else Sdf.Path("/Asset/geo")
                    self.assertEqual(prim.GetParent().GetPath(), expected_parent)
                    self.assertEqual(UsdGeom.Imageable(prim).GetVisibilityAttr().Get(),
                                     "invisible" if hidden == "1" else "inherited")
                    world, local = next(records), next(records)
                    self.assertEqual((world[0], local[0]), ("WORLD", "LOCAL"))
                    self._assert_close(cache.GetLocalToWorldTransform(prim),
                                       converted_matrix(list(map(float, world[1:])), scale))
                    expected_local = local if parent in objects else world
                    self._assert_close(UsdGeom.Xformable(prim).GetLocalTransformation(),
                                       converted_matrix(list(map(float, expected_local[1:])), scale))
                    if mesh_name:
                        mesh_objects.setdefault(mesh_name, []).append(
                            UsdGeom.Mesh(stage.GetPrimAtPath(prim.GetPath().AppendChild("mesh")))
                        )
                    else:
                        self.assertFalse(any(child.IsA(UsdGeom.Mesh) for child in prim.GetChildren()))
                for _ in range(int(mesh_count)):
                    tag, name, points, faces, corners, uv_count = next(records)
                    self.assertEqual(tag, "MESH")
                    expected_points = [converted_vector(list(map(float, next(records)[1:])), scale)
                                       for _ in range(int(points))]
                    counts, indices = next(records), next(records)
                    expected_normals = [converted_vector(list(map(float, next(records)[1:])))
                                        for _ in range(int(corners))]
                    uv_maps = []
                    for _ in range(int(uv_count)):
                        tag, uv_name, render = next(records)
                        self.assertEqual(tag, "UV")
                        values = [tuple(map(float, next(records)[1:])) for _ in range(int(corners))]
                        uv_maps.append(("st" if render == "1" else uv_name, values))
                    for mesh in mesh_objects[name]:
                        self._assert_close(mesh.GetPointsAttr().Get(), expected_points)
                        self.assertEqual(list(mesh.GetFaceVertexCountsAttr().Get()), list(map(int, counts[1:])))
                        self.assertEqual(len(mesh.GetFaceVertexCountsAttr().Get()), int(faces))
                        self.assertEqual(list(mesh.GetFaceVertexIndicesAttr().Get()), list(map(int, indices[1:])))
                        self._assert_close(mesh.GetNormalsAttr().Get(), expected_normals)
                        expected_extent = [
                            [operation(point[axis] for point in expected_points) for axis in range(3)]
                            for operation in (min, max)
                        ] if expected_points else []
                        self._assert_close(mesh.GetExtentAttr().Get(), expected_extent)
                        self.assertEqual(len(UsdGeom.PrimvarsAPI(mesh).GetAuthoredPrimvars()), len(uv_maps))
                        for identifier, values in uv_maps:
                            primvar = UsdGeom.PrimvarsAPI(mesh).GetPrimvar(identifier)
                            expected_values, expected_indices = [], []
                            for value in values:
                                if value not in expected_values:
                                    expected_values.append(value)
                                expected_indices.append(expected_values.index(value))
                            self.assertEqual(primvar.GetTypeName(), Sdf.ValueTypeNames.TexCoord2fArray)
                            self.assertEqual(primvar.GetInterpolation(), "faceVarying")
                            self.assertEqual(list(primvar.GetIndices()), expected_indices)
                            self._assert_close(primvar.Get(), expected_values, tolerance=0)
                            self._assert_close(primvar.ComputeFlattened(), values, tolerance=0)
                self.assertEqual(list(records), [])
                if fixture in EVALUATION:
                    self.assertTrue(all(not attr.GetTimeSamples()
                                        for prim in stage.Traverse() for attr in prim.GetAuthoredAttributes()))
                if fixture in [*SCENES, *FALLBACKS, *EVALUATION]:
                    self.assertEqual(len(mesh_objects["SharedGeometry"]), 3)
                    self.assertEqual(objects["mesh"].GetName(), "mesh_1")
                    self.assertNotIn("OutsideParent", objects)
                    if fixture in FALLBACKS:
                        for name in ("CameraFallback", "LightFallback", "TextFallback", "ImageFallback"):
                            self.assertEqual(objects[name].GetTypeName(), "Xform")
                        self.assertEqual(objects["SharedRoot"].GetParent(), objects["TextFallback"])
                else:
                    self.assertEqual(len(mesh_objects["Seams"]), 2)
                    self.assertEqual(len(objects), 5)
                    self.assertEqual(len(mesh_objects), 4)
                    for mesh in mesh_objects["Seams"]:
                        uv = UsdGeom.PrimvarsAPI(mesh).GetPrimvar("Seams")
                        self.assertEqual(list(uv.GetIndices()), [0, 1, 2, 0, 3, 4, 1])
                        self.assertEqual(math.copysign(1, uv.Get()[0][1]), -1)
                self.assertFalse(any(prim.IsInstance() for prim in stage.Traverse()))

    def test_saved_normal_oracles(self):
        for fixture in [*LEGACY_NORMALS, *POLYGON_NORMALS]:
            with self.subTest(fixture=str(fixture)):
                legacy = fixture in LEGACY_NORMALS
                stage = Usd.Stage.Open(str(fixture))
                self._assert_contract(stage, "3.3" if legacy else "5.2", "Normals")
                records = iter(fixture.with_suffix(".oracle.txt").read_text(encoding="ascii").splitlines())
                self.assertEqual(next(records), "BLEND_NORMALS_ORACLE 3" if legacy else "BLEND_NORMALS_ORACLE 1")
                self.assertEqual(next(records), "'3.3.21'" if legacy else "'5.2.2 LTS'")
                scale, count = next(records).split()
                self.assertEqual(int(count), 1)
                name, points, faces, corners = shlex.split(next(records))
                if legacy:
                    marker, enabled, angle = next(records).split()
                    self.assertEqual((marker, enabled), ("AUTO_SMOOTH", "1"))
                    self.assertGreaterEqual(float(angle), 0)
                    self.assertLessEqual(float(angle), math.pi + 1e-7)
                mesh = UsdGeom.Mesh(stage.GetPrimAtPath(f"/Asset/geo/{name}/mesh"))
                self.assertTrue(mesh)
                expected_points = [converted_vector(tuple(map(float, next(records).split())), float(scale))
                                   for _ in range(int(points))]
                self._assert_close(mesh.GetPointsAttr().Get(), expected_points)
                counts = list(map(int, next(records).split()))
                indices = list(map(int, next(records).split()))
                self.assertEqual(len(counts), int(faces))
                self.assertEqual(len(indices), int(corners))
                self.assertEqual(list(mesh.GetFaceVertexCountsAttr().Get()), counts)
                self.assertEqual(list(mesh.GetFaceVertexIndicesAttr().Get()), indices)
                expected_normals = [converted_vector(tuple(map(float, next(records).split())))
                                    for _ in range(int(corners))]
                normals = mesh.GetNormalsAttr().Get()
                self.assertEqual(mesh.GetNormalsInterpolation(), "faceVarying")
                self.assertEqual(len(normals), int(corners))
                for actual, expected in zip(normals, expected_normals):
                    for component, target in zip(actual, expected):
                        self.assertLessEqual(abs(component - target), 2e-5)
                self.assertEqual(list(records), [])
                referenced = Usd.Stage.CreateInMemory()
                referenced.DefinePrim("/Referenced").GetReferences().AddReference(str(fixture))
                other = UsdGeom.Mesh(referenced.GetPrimAtPath(f"/Referenced/geo/{name}/mesh"))
                self.assertEqual(other.GetNormalsAttr().Get(), normals)

    def test_multi_scale_imports(self):
        baseline = None
        for version in ("blender-4.5.13", "blender-5.2.2"):
            for unit in ("1m", "1cm", "1mm", "10m"):
                fixture = NATIVE / "native-units" / version / f"unit-{unit}.blend"
                with self.subTest(fixture=str(fixture)):
                    stage = Usd.Stage.Open(str(fixture))
                    self._assert_contract(stage, "4.5" if "4.5" in version else "5.2", "Units")
                    cache = UsdGeom.XformCache()
                    values = {}
                    for prim in stage.Traverse():
                        if prim.IsA(UsdGeom.Mesh):
                            mesh = UsdGeom.Mesh(prim)
                            values[str(prim.GetPath())] = list(mesh.GetPointsAttr().Get())
                        elif prim.IsA(UsdGeom.Xform):
                            values[str(prim.GetPath())] = cache.GetLocalToWorldTransform(prim)
                    if baseline is None:
                        baseline = values
                    self.assertEqual(values.keys(), baseline.keys())
                    for path, value in values.items():
                        self._assert_close(value, [list(row) for row in baseline[path]])


if __name__ == "__main__":
    unittest.main()
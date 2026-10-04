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
MESHES = [NATIVE / "native-mesh" / version / "mesh.blend" for version in ("blender-4.5.13", "blender-5.2.2")]


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
        with self.assertRaises(Tf.ErrorException) as error:
            Usd.Stage.Open(str(corpus))
        self.assertIn("BLEND_SCENE_OBJECT_TYPE_UNSUPPORTED", str(error.exception))

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
            (gzip.compress(data), "BLEND_BLOCK_COMPRESSED"),
            (b"\x28\xb5\x2f\xfd" + b"\x00" * 20, "BLEND_BLOCK_COMPRESSED"),
        ]
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

    def test_repeat_read_and_metadata(self):
        for fixture in [FIXTURES / "single_cube.blend", *SCENES, *MESHES]:
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
        for fixture in [FIXTURES / "single_cube.blend", *SCENES, *MESHES]:
            with self.subTest(fixture=str(fixture)):
                stage = Usd.Stage.CreateInMemory()
                root = stage.DefinePrim("/Referenced")
                root.GetReferences().AddReference(str(fixture))
                self.assertEqual(root.GetCustomDataByKey("blend:stageContractVersion"), 1)
                self.assertTrue(stage.GetPrimAtPath("/Referenced/geo"))
                self.assertTrue(stage.GetPrimAtPath("/Referenced/mtl"))
                self.assertTrue(any(prim.IsA(UsdGeom.Mesh) for prim in stage.Traverse()))

    def test_integrated_scene_oracles(self):
        for fixture in [*SCENES, *MESHES]:
            with self.subTest(fixture=str(fixture)):
                stage = Usd.Stage.Open(str(fixture))
                self._assert_contract(stage, "4.5" if "4.5" in str(fixture) else "5.2",
                                      "Integrated" if fixture in SCENES else "MeshDomains")
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
                if fixture in SCENES:
                    self.assertEqual(len(mesh_objects["SharedGeometry"]), 3)
                    self.assertEqual(objects["mesh"].GetName(), "mesh_1")
                    self.assertNotIn("OutsideParent", objects)
                else:
                    self.assertEqual(len(mesh_objects["Seams"]), 2)
                    self.assertEqual(len(objects), 5)
                    self.assertEqual(len(mesh_objects), 4)
                    for mesh in mesh_objects["Seams"]:
                        uv = UsdGeom.PrimvarsAPI(mesh).GetPrimvar("Seams")
                        self.assertEqual(list(uv.GetIndices()), [0, 1, 2, 0, 3, 4, 1])
                        self.assertEqual(math.copysign(1, uv.Get()[0][1]), -1)
                self.assertFalse(any(prim.IsInstance() for prim in stage.Traverse()))

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
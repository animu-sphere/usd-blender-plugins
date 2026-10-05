import argparse
import gzip
import subprocess
import sys
import unittest
from pathlib import Path
from tempfile import TemporaryDirectory

from pxr import Sdf, Tf, Usd, UsdGeom

from test_stage import raw_zstd_frame


FIXTURE = (Path(__file__).resolve().parents[3] / "tests" / "fixtures" /
           "native-scene" / "blender-5.2.2" / "instances.blend")


class InstanceGraphTests(unittest.TestCase):
    cases_executable: Path

    def assert_source_stage(self, layer):
        stage = Usd.Stage.Open(layer)
        self.assertEqual(
            [str(prim.GetPath()) for prim in stage.Traverse()],
            ["/Asset", "/Asset/geo", "/Asset/geo/Child",
             "/Asset/geo/RootInstance", "/Asset/geo/SharedInstance", "/Asset/mtl"],
        )
        self.assertEqual(stage.GetDefaultPrim().GetCustomDataByKey("blend:sourceScene"), "Instances")
        self.assertEqual(stage.GetDefaultPrim().GetCustomDataByKey("blend:sourceVersion"), "5.2")
        self.assertEqual(UsdGeom.GetStageMetersPerUnit(stage), 1.0)
        self.assertEqual(UsdGeom.GetStageUpAxis(stage), "Y")
        child = UsdGeom.Xformable(stage.GetPrimAtPath("/Asset/geo/Child"))
        self.assertEqual(tuple(child.GetLocalTransformation().ExtractTranslation()), (2.0, 6.0, -4.0))
        self.assertTrue(all(not prim.IsInstance() for prim in stage.Traverse()))
        return stage

    def test_registered_recursive_graph_cases(self):
        with TemporaryDirectory(prefix="blend-instance-plugin-") as directory:
            root = Path(directory)
            result = subprocess.run(
                [str(self.cases_executable), str(FIXTURE), "--write-cases", str(root)],
                capture_output=True, text=True, timeout=120,
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            cases = sorted(root.glob("*.expected.txt"))
            self.assertEqual(len(cases), 24)
            for expected in cases:
                code, offset, block = expected.read_text(encoding="ascii").split()
                data = expected.with_suffix("").with_suffix(".blend").read_bytes()
                for encoding, payload in (
                    ("plain", data), ("gzip", gzip.compress(data, mtime=0)),
                    ("zstd", raw_zstd_frame(data)),
                ):
                    path = root / f"{expected.name}-{encoding}.blend"
                    path.write_bytes(payload)
                    for metadata_only in (False, True):
                        with self.subTest(case=expected.name, encoding=encoding, metadata=metadata_only):
                            if code == "SUCCESS":
                                first = Sdf.Layer.OpenAsAnonymous(str(path), metadataOnly=metadata_only)
                                second = Sdf.Layer.OpenAsAnonymous(str(path), metadataOnly=metadata_only)
                                stage = self.assert_source_stage(first)
                                self.assertEqual(first.ExportToString(), second.ExportToString())
                                referenced = Usd.Stage.CreateInMemory()
                                asset = referenced.DefinePrim("/Referenced", "Xform")
                                asset.GetReferences().AddReference(str(path))
                                self.assertTrue(referenced.GetPrimAtPath("/Referenced/geo/Child"))
                                self.assertEqual(
                                    UsdGeom.Xformable(referenced.GetPrimAtPath("/Referenced/geo/Child")).GetLocalTransformation(),
                                    UsdGeom.Xformable(stage.GetPrimAtPath("/Asset/geo/Child")).GetLocalTransformation(),
                                )
                            else:
                                for _ in range(2):
                                    with self.assertRaises(Tf.ErrorException) as error:
                                        Sdf.Layer.OpenAsAnonymous(str(path), metadataOnly=metadata_only)
                                    message = str(error.exception)
                                    self.assertIn(code, message)
                                    self.assertIn(f"[byte {offset}]", message)
                                    self.assertIn(f"[block {block}]", message)
                                    if code != "BLEND_SCENE_INSTANCE_UNSUPPORTED":
                                        self.assertNotIn("BLEND_SCENE_INSTANCE_UNSUPPORTED", message)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--cases-executable", required=True, type=Path)
    options, tests = parser.parse_known_args()
    InstanceGraphTests.cases_executable = options.cases_executable.resolve()
    unittest.main(argv=[sys.argv[0], *tests])

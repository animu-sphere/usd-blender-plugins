import unittest
from pathlib import Path

from pxr import Sdf, Tf, Usd, UsdGeom


FIXTURES = Path(__file__).resolve().parent / "fixtures"


class StageContractTests(unittest.TestCase):
    def test_minimal_stage(self):
        self._assert_minimal_stage("header_only.blend", "4.5")

    def test_blender_written_stage(self):
        self._assert_minimal_stage("empty.blend", "5.2")

    def _assert_minimal_stage(self, fixture, source_version):
        stage = Usd.Stage.Open(str(FIXTURES / fixture))
        self.assertIsNotNone(stage)
        self.assertEqual(str(stage.GetDefaultPrim().GetPath()), "/Asset")
        self.assertEqual(stage.GetDefaultPrim().GetTypeName(), "Xform")
        self.assertEqual(stage.GetDefaultPrim().GetMetadata("kind"), "component")
        self.assertEqual(stage.GetDefaultPrim().GetCustomDataByKey("blend:stageContractVersion"), 1)
        self.assertEqual(stage.GetDefaultPrim().GetCustomDataByKey("blend:sourceVersion"), source_version)
        self.assertEqual(UsdGeom.GetStageUpAxis(stage), "Y")
        self.assertEqual(UsdGeom.GetStageMetersPerUnit(stage), 1.0)
        self.assertEqual(
            [(str(prim.GetPath()), prim.GetTypeName()) for prim in stage.Traverse()],
            [("/Asset", "Xform"), ("/Asset/geo", "Scope"), ("/Asset/mtl", "Scope")],
        )
        self.assertEqual(stage.GetRootLayer().customLayerData, {})

    def test_header_failures(self):
        cases = {
            "invalid.blend": "BLEND_HEADER_MAGIC",
            "truncated.blend": "BLEND_HEADER_TRUNCATED",
            "pointer_size.blend": "BLEND_HEADER_POINTER_SIZE",
            "endianness.blend": "BLEND_HEADER_ENDIANNESS",
            "version.blend": "BLEND_HEADER_VERSION",
        }
        for fixture, code in cases.items():
            with self.subTest(fixture=fixture):
                with self.assertRaises(Tf.ErrorException) as error:
                    Usd.Stage.Open(str(FIXTURES / fixture))
                self.assertIn(code, str(error.exception))

    def test_repeat_read_and_metadata(self):
        for fixture in ("header_only.blend", "empty.blend"):
            with self.subTest(fixture=fixture):
                path = str(FIXTURES / fixture)
                first = Sdf.Layer.OpenAsAnonymous(path)
                second = Sdf.Layer.OpenAsAnonymous(path)
                metadata = Sdf.Layer.OpenAsAnonymous(path, metadataOnly=True)
                self.assertEqual(first.ExportToString(), second.ExportToString())
                self.assertEqual(first.ExportToString(), metadata.ExportToString())

    def test_contract_survives_reference(self):
        for fixture in ("header_only.blend", "empty.blend"):
            with self.subTest(fixture=fixture):
                stage = Usd.Stage.CreateInMemory()
                root = stage.DefinePrim("/Referenced")
                root.GetReferences().AddReference(str(FIXTURES / fixture))
                self.assertEqual(root.GetCustomDataByKey("blend:stageContractVersion"), 1)
                self.assertTrue(stage.GetPrimAtPath("/Referenced/geo"))
                self.assertTrue(stage.GetPrimAtPath("/Referenced/mtl"))


if __name__ == "__main__":
    unittest.main()
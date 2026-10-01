import unittest
from pathlib import Path

from pxr import Sdf, Tf, Usd, UsdGeom


FIXTURES = Path(__file__).resolve().parent / "fixtures"


class StageContractTests(unittest.TestCase):
    def test_minimal_stage(self):
        stage = Usd.Stage.Open(str(FIXTURES / "header_only.blend"))
        self.assertIsNotNone(stage)
        self.assertEqual(str(stage.GetDefaultPrim().GetPath()), "/Asset")
        self.assertEqual(stage.GetDefaultPrim().GetTypeName(), "Xform")
        self.assertEqual(stage.GetDefaultPrim().GetMetadata("kind"), "component")
        self.assertEqual(stage.GetDefaultPrim().GetCustomDataByKey("blend:stageContractVersion"), 1)
        self.assertEqual(stage.GetDefaultPrim().GetCustomDataByKey("blend:sourceVersion"), "4.5")
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
        path = str(FIXTURES / "header_only.blend")
        first = Sdf.Layer.OpenAsAnonymous(path)
        second = Sdf.Layer.OpenAsAnonymous(path)
        metadata = Sdf.Layer.OpenAsAnonymous(path, metadataOnly=True)
        self.assertEqual(first.ExportToString(), second.ExportToString())
        self.assertEqual(first.ExportToString(), metadata.ExportToString())

    def test_contract_survives_reference(self):
        stage = Usd.Stage.CreateInMemory()
        root = stage.DefinePrim("/Referenced")
        root.GetReferences().AddReference(str(FIXTURES / "header_only.blend"))
        self.assertEqual(root.GetCustomDataByKey("blend:stageContractVersion"), 1)
        self.assertTrue(stage.GetPrimAtPath("/Referenced/geo"))
        self.assertTrue(stage.GetPrimAtPath("/Referenced/mtl"))


if __name__ == "__main__":
    unittest.main()
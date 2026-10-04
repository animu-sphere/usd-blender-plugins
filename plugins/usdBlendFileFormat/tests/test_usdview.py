import os
import time
import unittest
from pathlib import Path

from pxr import Sdf, UsdGeom
from pxr.Usdviewq.common import ClearColors
from pxr.Usdviewq.qt import QtWidgets


def testUsdviewInputFunction(appController):
    try:
        _verify_cube(appController)
    finally:
        QtWidgets.QApplication.closeAllWindows()


def _verify_cube(appController):
    check = unittest.TestCase()
    api = appController._usdviewApi
    stage = api.stage
    check.assertEqual(Path(stage.GetRootLayer().realPath).name, "single_cube.blend")
    check.assertEqual(stage.GetRootLayer().GetFileFormat().formatId, "blend")
    check.assertEqual(str(stage.GetDefaultPrim().GetPath()), "/Asset")
    check.assertEqual(UsdGeom.GetStageUpAxis(stage), "Y")
    check.assertEqual(UsdGeom.GetStageMetersPerUnit(stage), 1.0)
    mesh_path = Sdf.Path("/Asset/geo/Cube/mesh")
    check.assertTrue(stage.GetPrimAtPath(mesh_path).IsA(UsdGeom.Mesh))

    view = appController._stageView
    check.assertIsNotNone(view, "A rendered viewport is required; do not use --norender")
    check.assertEqual(api.GetViewportCurrentRendererId(), "HdStormRendererPlugin")
    settings = appController._dataModel.viewSettings
    settings.clearColorText = ClearColors.BLACK
    settings.showHUD = False
    settings.showBBoxes = False
    view.updateView(resetCam=True, forceComputeBBox=True, frameFit=2.0)
    settings.freeCamera.rotTheta = 35.0
    settings.freeCamera.rotPhi = 25.0
    appController._processEvents()
    deadline = time.monotonic() + 30.0
    while True:
        view.updateGL()
        QtWidgets.QApplication.processEvents()
        if view.IsRendererConverged():
            break
        if time.monotonic() >= deadline:
            check.fail("Storm did not converge within 30 seconds")
        time.sleep(0.01)

    image = api.GrabViewportShot()
    check.assertIsNotNone(image)
    check.assertFalse(image.isNull(), "Viewport capture must contain pixels")
    check.assertEqual(image.pixelColor(0, 0).getRgb()[:3], (0, 0, 0))
    check.assertNotEqual(image.pixelColor(image.width() // 2, image.height() // 2).getRgb()[:3],
                         (0, 0, 0), "The rendered Cube must differ from the background")
    screenshot = os.environ.get("USD_BLEND_USDVIEW_SCREENSHOT")
    if screenshot:
        check.assertTrue(image.save(screenshot, "PNG"), f"Could not save {screenshot}")
    width, height = view.GetPhysicalWindowSize()
    in_bounds, frustum = view.computePickFrustum(width / 2, height / 2)
    check.assertTrue(in_bounds)
    hits = view.pick(frustum)
    check.assertTrue(hits, "The viewport center must hit the rendered Cube")
    check.assertEqual(hits[0].hitPrimPath, mesh_path)
    print(f"Verified direct .blend rendering with Storm: {mesh_path}")

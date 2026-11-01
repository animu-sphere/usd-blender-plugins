import os
import time
import unittest
from pathlib import Path

from pxr import Gf, Sdf, UsdGeom, UsdShade
from pxr.Usdviewq.common import ClearColors, SelectionHighlightModes
from pxr.Usdviewq.qt import QtWidgets


def testUsdviewInputFunction(appController):
    try:
        _verify_texture(appController)
    finally:
        QtWidgets.QApplication.closeAllWindows()


def _verify_texture(appController):
    check = unittest.TestCase()
    api = appController._usdviewApi
    stage = api.stage
    check.assertEqual(Path(stage.GetRootLayer().realPath).name, "textures.blend")
    check.assertEqual(stage.GetRootLayer().GetFileFormat().formatId, "blend")
    check.assertEqual(api.GetViewportCurrentRendererId(), "HdStormRendererPlugin")
    mesh_path = Sdf.Path("/Asset/geo/Relative/mesh")
    mesh = stage.GetPrimAtPath(mesh_path)
    check.assertTrue(mesh.IsA(UsdGeom.Mesh))
    material = UsdShade.MaterialBindingAPI(mesh).ComputeBoundMaterial()[0]
    check.assertEqual(str(material.GetPath()), "/Asset/mtl/Relative")
    graph = "/Asset/mtl/Relative/preview"
    texture = UsdShade.Shader(stage.GetPrimAtPath(graph + "/BaseColorTexture"))
    reader = UsdShade.Shader(stage.GetPrimAtPath(graph + "/StReader"))
    surface = UsdShade.Shader(stage.GetPrimAtPath(graph + "/Surface"))
    check.assertEqual(texture.GetIdAttr().Get(), "UsdUVTexture")
    check.assertEqual(texture.GetInput("file").Get().path, "./textures/color.png")
    check.assertTrue(Path(texture.GetInput("file").Get().resolvedPath).is_file())
    check.assertEqual(texture.GetInput("sourceColorSpace").Get(), "sRGB")
    check.assertEqual(surface.GetInput("diffuseColor").GetAttr().GetConnections(),
                      [texture.GetOutput("rgb").GetAttr().GetPath()])
    check.assertEqual(reader.GetInput("varname").Get(), "st")
    check.assertEqual(texture.GetInput("st").GetAttr().GetConnections(),
                      [reader.GetOutput("result").GetAttr().GetPath()])

    stage.SetEditTarget(stage.GetSessionLayer())
    for obj in stage.GetPrimAtPath("/Asset/geo").GetChildren():
        if obj.GetName() != "Relative":
            check.assertTrue(obj.SetActive(False))

    view = appController._stageView
    check.assertIsNotNone(view, "A rendered viewport is required; do not use --norender")
    settings = appController._dataModel.viewSettings
    settings.clearColorText = ClearColors.BLACK
    settings.selHighlightMode = SelectionHighlightModes.NEVER
    settings.showHUD = False
    settings.showBBoxes = False
    view.updateView(resetCam=True, forceComputeBBox=True, frameFit=2.0)
    settings.freeCamera.rotTheta = 35.0
    settings.freeCamera.rotPhi = 25.0
    appController._processEvents()

    def capture():
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
        check.assertFalse(image.isNull())
        return image

    def count_colors(image):
        width, height = image.width(), image.height()
        red = yellow = 0
        for x in range(width // 4, 3 * width // 4, 2):
            for y in range(2 * height // 5, 2 * height // 3, 2):
                r, g, b = image.pixelColor(x, y).getRgb()[:3]
                red += r > 80 and r > 1.35 * g and r > 1.35 * b
                yellow += r > 100 and g > 100 and b < 0.5 * min(r, g)
        return red, yellow

    image = capture()
    screenshot = os.environ.get("USD_BLEND_USDVIEW_SCREENSHOT")
    if screenshot:
        check.assertTrue(image.save(screenshot, "PNG"), f"Could not save {screenshot}")
    red, yellow = count_colors(image)
    check.assertGreater(red, 100, "The texture's red region must be visible")
    check.assertGreater(yellow, 100, "The filtered texture must include a red/green region")
    width, height = view.GetPhysicalWindowSize()
    in_bounds, frustum = view.computePickFrustum(width / 2, height / 2)
    check.assertTrue(in_bounds)
    hits = view.pick(frustum)
    check.assertTrue(hits)
    check.assertEqual(hits[0].hitPrimPath, mesh_path)
    diffuse = surface.GetInput("diffuseColor")
    check.assertTrue(diffuse.DisconnectSource())
    check.assertTrue(diffuse.Set(Gf.Vec3f(0.2, 0.4, 0.6)))
    fallback_red, fallback_yellow = count_colors(capture())
    check.assertLess(4 * fallback_red, red, "The constant fallback must not mimic the red texture")
    check.assertLess(4 * fallback_yellow, yellow, "The constant fallback must not mimic the filtered texture")
    print(f"Verified direct .blend texture rendering with Storm: {mesh_path} "
          f"(texture red/red-green: {red}/{yellow}; "
          f"constant red/red-green: {fallback_red}/{fallback_yellow})")

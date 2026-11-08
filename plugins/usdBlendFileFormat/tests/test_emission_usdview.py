import time
import unittest
from pathlib import Path

from pxr import Gf, Sdf, UsdGeom, UsdShade
from pxr.Usdviewq.common import ClearColors, RenderModes, SelectionHighlightModes
from pxr.Usdviewq.qt import QtWidgets


def testUsdviewInputFunction(appController):
    try:
        _verify_emission(appController)
    finally:
        QtWidgets.QApplication.closeAllWindows()


def _verify_emission(appController):
    check = unittest.TestCase()
    api = appController._usdviewApi
    stage = api.stage
    check.assertEqual(Path(stage.GetRootLayer().realPath).name, "materials.blend")
    check.assertEqual(stage.GetRootLayer().GetFileFormat().formatId, "blend")
    check.assertEqual(api.GetViewportCurrentRendererId(), "HdStormRendererPlugin")
    mesh_path = Sdf.Path("/Asset/geo/Deferred/mesh")
    mesh = stage.GetPrimAtPath(mesh_path)
    check.assertTrue(mesh.IsA(UsdGeom.Mesh))
    material = UsdShade.MaterialBindingAPI(mesh).ComputeBoundMaterial()[0]
    check.assertEqual(str(material.GetPath()), "/Asset/mtl/Deferred")
    surface = UsdShade.Shader(stage.GetPrimAtPath("/Asset/mtl/Deferred/preview/Surface"))
    check.assertEqual(surface.GetIdAttr().Get(), "UsdPreviewSurface")
    emission = surface.GetInput("emissiveColor")
    check.assertEqual(emission.Get(), Gf.Vec3f(3, 3, 3))
    check.assertEqual(surface.GetInput("opacity").Get(), 0.25)
    graph_output = UsdShade.NodeGraph(stage.GetPrimAtPath("/Asset/mtl/Deferred/preview")).GetOutput("surface")
    check.assertEqual(graph_output.GetAttr().GetConnections(),
                      [surface.GetOutput("surface").GetAttr().GetPath()])

    stage.SetEditTarget(stage.GetSessionLayer())
    for obj in stage.GetPrimAtPath("/Asset/geo").GetChildren():
        if obj.GetName() != "Deferred":
            check.assertTrue(obj.SetActive(False))
    check.assertTrue(surface.GetInput("diffuseColor").Set(Gf.Vec3f(0, 0, 0)))
    # Keep this an emission-only check; the saved Alpha is translucent.
    check.assertTrue(surface.GetInput("opacity").Set(1.0))

    view = appController._stageView
    check.assertIsNotNone(view, "A rendered viewport is required; do not use --norender")
    settings = appController._dataModel.viewSettings
    settings.clearColorText = ClearColors.BLACK
    settings.selHighlightMode = SelectionHighlightModes.NEVER
    settings.showHUD = False
    settings.showBBoxes = False
    settings.renderMode = RenderModes.SMOOTH_SHADED
    settings.enableSceneMaterials = True
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

    def set_emission(value):
        check.assertTrue(emission.Set(value))
        # Refresh Storm's material network after editing a constant input.
        check.assertTrue(graph_output.DisconnectSource())
        capture()
        check.assertTrue(graph_output.ConnectToSource(surface.ConnectableAPI(), "surface"))

    def changed_pixels(first, second):
        check.assertEqual((first.width(), first.height()), (second.width(), second.height()))
        changed = 0
        for x in range(first.width() // 4, 3 * first.width() // 4, 2):
            for y in range(2 * first.height() // 5, 2 * first.height() // 3, 2):
                a = first.pixelColor(x, y).getRgb()[:3]
                b = second.pixelColor(x, y).getRgb()[:3]
                changed += max(abs(left - right) for left, right in zip(a, b)) > 10
        return changed

    set_emission(Gf.Vec3f(3, 3, 3))
    saved = capture()
    width, height = view.GetPhysicalWindowSize()
    in_bounds, frustum = view.computePickFrustum(width / 2, height / 2)
    check.assertTrue(in_bounds)
    hits = view.pick(frustum)
    check.assertTrue(hits)
    check.assertEqual(hits[0].hitPrimPath, mesh_path)
    images = []
    for value in (Gf.Vec3f(0, 0, 0), Gf.Vec3f(0.25, 0.5, 0.75),
                  Gf.Vec3f(0.75, 1.5, 2.25), Gf.Vec3f(3, 3, 3)):
        set_emission(value)
        images.append(capture())

    zero, strength_one, strength_three, restored = images
    saved_effect = changed_pixels(saved, zero)
    strength_effect = changed_pixels(strength_one, strength_three)
    restored_diff = changed_pixels(saved, restored)
    center = (saved.width() // 2, saved.height() // 2)
    one_rgb = strength_one.pixelColor(*center).getRgb()[:3]
    three_rgb = strength_three.pixelColor(*center).getRgb()[:3]
    check.assertGreater(saved_effect, 100, "Saved emission must affect the rendered surface")
    check.assertGreater(strength_effect, 100, "Increasing emission strength must affect the rendered surface")
    check.assertLess(4 * restored_diff, saved_effect, "Restoring saved emission must restore the image")
    check.assertLess(one_rgb[0], one_rgb[1])
    check.assertLess(one_rgb[1], one_rgb[2])
    for one, three in zip(one_rgb, three_rgb):
        check.assertGreater(three, one, "Each emission channel must brighten as strength increases")
    print(f"Verified Storm emission visibility: {mesh_path} "
          f"(saved/strength/restored changed samples: {saved_effect}/{strength_effect}/{restored_diff}; "
          f"center RGB strength 1/3: {one_rgb}/{three_rgb})")

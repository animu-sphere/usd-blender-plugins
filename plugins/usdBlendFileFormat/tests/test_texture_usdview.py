import os
import time
import unittest
from pathlib import Path

from pxr import Gf, Sdf, UsdGeom, UsdShade, Vt
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

    def changed_pixels(first, second):
        check.assertEqual((first.width(), first.height()), (second.width(), second.height()))
        changed = 0
        for x in range(first.width() // 4, 3 * first.width() // 4, 2):
            for y in range(2 * first.height() // 5, 2 * first.height() // 3, 2):
                a = first.pixelColor(x, y).getRgb()[:3]
                b = second.pixelColor(x, y).getRgb()[:3]
                changed += max(abs(left - right) for left, right in zip(a, b)) > 10
        return changed

    for name, reader_name, varname in (
        ("Normal", "StReader", "st"),
        ("NamedNormal", "NormalStReader", "Detail_UV_1"),
    ):
        for obj in stage.GetPrimAtPath("/Asset/geo").GetAllChildren():
            check.assertTrue(obj.SetActive(obj.GetName() == name))
        mesh_path = Sdf.Path(f"/Asset/geo/{name}/mesh")
        mesh = stage.GetPrimAtPath(mesh_path)
        material = UsdShade.MaterialBindingAPI(mesh).ComputeBoundMaterial()[0]
        check.assertEqual(str(material.GetPath()), f"/Asset/mtl/{name}")
        graph = f"/Asset/mtl/{name}/preview"
        texture = UsdShade.Shader(stage.GetPrimAtPath(graph + "/NormalTexture"))
        reader = UsdShade.Shader(stage.GetPrimAtPath(graph + "/" + reader_name))
        surface = UsdShade.Shader(stage.GetPrimAtPath(graph + "/Surface"))
        check.assertEqual(texture.GetIdAttr().Get(), "UsdUVTexture")
        check.assertEqual(texture.GetInput("file").Get().path, "./textures/color.png")
        check.assertEqual(texture.GetInput("sourceColorSpace").Get(), "raw")
        check.assertTrue(Path(texture.GetInput("file").Get().resolvedPath).is_file())
        check.assertEqual(texture.GetInput("scale").Get(), Gf.Vec4f(2, 2, 2, 1))
        check.assertEqual(texture.GetInput("bias").Get(), Gf.Vec4f(-1, -1, -1, 0))
        check.assertEqual(reader.GetInput("varname").Get(), varname)
        check.assertEqual(texture.GetInput("st").GetAttr().GetConnections(),
                          [reader.GetOutput("result").GetAttr().GetPath()])
        normal = surface.GetInput("normal")
        output = texture.GetOutput("rgb")
        check.assertEqual(normal.GetAttr().GetConnections(), [output.GetAttr().GetPath()])
        mapped = capture()
        hits = view.pick(frustum)
        check.assertTrue(hits)
        check.assertEqual(hits[0].hitPrimPath, mesh_path)
        check.assertTrue(normal.DisconnectSource())
        geometric = capture()
        check.assertTrue(normal.ConnectToSource(output))
        restored = capture()
        changed = changed_pixels(mapped, geometric)
        repeat = changed_pixels(mapped, restored)
        check.assertGreater(changed, 100, f"{name}: normal map must affect the rendered surface")
        check.assertLess(4 * repeat, changed, f"{name}: restoring the map must restore the image")
        print(f"Verified Storm normal-map visibility: {mesh_path} "
              f"(changed samples: {changed}; restored differences: {repeat})")

        if name == "NamedNormal":
            primvars = UsdGeom.PrimvarsAPI(mesh)
            render_uv = primvars.GetPrimvar("st")
            named_uv = primvars.GetPrimvar(varname)
            original_render_uv = render_uv.Get()
            original_named_uv = named_uv.Get()
            check.assertTrue(original_render_uv)
            check.assertTrue(original_named_uv)
            shift = Gf.Vec2f(0.5, 0)
            check.assertTrue(render_uv.Set(Vt.Vec2fArray([uv + shift for uv in original_render_uv])))
            unrelated = capture()
            check.assertTrue(named_uv.Set(Vt.Vec2fArray([uv + shift for uv in original_named_uv])))
            shifted = capture()
            check.assertTrue(normal.DisconnectSource())
            shifted_geometric = capture()
            check.assertTrue(named_uv.Set(original_named_uv))
            original_geometric = capture()
            check.assertTrue(render_uv.Set(original_render_uv))
            check.assertTrue(normal.ConnectToSource(output))
            uv_restored = capture()
            selected_diff = changed_pixels(unrelated, shifted)
            unrelated_diff = changed_pixels(restored, unrelated)
            geometric_diff = changed_pixels(shifted_geometric, original_geometric)
            restored_diff = changed_pixels(restored, uv_restored)
            check.assertGreater(selected_diff, 100, "The selected named UV must affect normal-map rendering")
            check.assertLess(4 * unrelated_diff, selected_diff, "Changing st must not mimic the selected UV")
            check.assertLess(4 * geometric_diff, selected_diff, "The UV change must not alter geometric shading")
            check.assertLess(4 * restored_diff, selected_diff, "Restoring UVs must restore the image")
            print(f"Verified Storm named normal UV selection: {mesh_path} "
                  f"(selected/unrelated/geometric/restored differences: "
                  f"{selected_diff}/{unrelated_diff}/{geometric_diff}/{restored_diff})")

# Stage contract

> Status: **proposed**, 2026-10-01. This document defines intended behavior;
> the [capability matrix](../reference/CAPABILITY_MATRIX.md) owns fixture-backed
> support claims. A section
> becomes binding when the Phase that first authors it lands with a fixture
> ([DESIGN_POLICY.md §14](DESIGN_POLICY.md#14-phases)); from then, changing it
> is a contract change (§2).
>
> This document owns the exact stage `usdBlendFileFormat` authors: hierarchy,
> prim types, metadata, the coordinate conversion, and the per-concept layout.
> On this area it wins over the design policy. Materials are detailed in
> [MATERIAL_POLICY.md](MATERIAL_POLICY.md), identifiers in
> [NAMING_POLICY.md](NAMING_POLICY.md).

## 1. Scope

A `.blend` opened through the file format becomes one layer whose content is
fixed by this document, whatever the source's scene or collection names are.
The layout is the `/Asset` contract shared with `usd-mmd-plugins` and
`usd-vrm-plugins`, so that stages from every animu-sphere file format compose
the same way.

The source is the `.blend`'s **active scene** — the one recorded in the file's
global block — and the objects linked into it through its collection
hierarchy. Other scenes, and objects linked into none, are not authored
([BLEND_CONTRACT.md §10](BLEND_CONTRACT.md#10-source-concepts)).

## 2. Contract version

The stage carries its contract version on the root prim, where it survives
referencing:

```usda
def Xform "Asset" (
    customData = {
        int "blend:stageContractVersion" = 1
    }
)
```

Layer metadata is not composed, so a version in `customLayerData` would be lost
once the asset is referenced; `/Asset` customData travels with the reference.

The version increases when an authored path, prim type, attribute name, type
or meaning changes. Adding a new optional prim or attribute that no existing
consumer misreads does not change it. The product version (`VERSION`) and the
contract version are independent
([WORKSPACE.md §4](../architecture/WORKSPACE.md#4-manifests-versioning-and-build-metadata)).

## 3. Authoring conventions

- Standard schemas first ([DESIGN_POLICY.md §2.1](DESIGN_POLICY.md#21-openusd-first)).
- A preserved source value with no standard home is a custom attribute in the
  `blend:` namespace, `custom = true`, never a new schema
  ([DESIGN_POLICY.md §6](DESIGN_POLICY.md#6-the-schema-admission-test)).
- Provenance (source name, datablock type, Blender version) is `customData`,
  not attributes.
- Every authored value is in the USD basis and meters (§6).
- Specifiers are `def`. Nothing is authored as `over` or `class`.
- Prims and properties are authored in a deterministic order: scopes in the
  order of §4, children in identifier order
  ([NAMING_POLICY.md §4](NAMING_POLICY.md#4-collisions-and-order)).

## 4. Prim hierarchy

```text
/Asset                    UsdGeomXform, defaultPrim
├─ geo                    UsdGeomScope — always authored
│  └─ <Object>            UsdGeomXform, one per geometry or empty object
│     ├─ mesh             UsdGeomMesh (or curves, points: §9)
│     └─ <ChildObject>    Object parenting, preserved
├─ mtl                    UsdGeomScope — always authored
│  └─ <Material>          UsdShadeMaterial
├─ skel                   UsdGeomScope — when the scene has armatures
├─ cameras                UsdGeomScope — when the scene has cameras
├─ lights                 UsdGeomScope — when the scene has lights
└─ collections            UsdGeomScope — when the scene has collections
```

Fixed rules:

| Rule | Value |
| --- | --- |
| root prim | `/Asset`, `UsdGeomXform` |
| default prim | `Asset` |
| geometry root | `/Asset/geo` |
| material root | `/Asset/mtl` |
| `/Asset` kind | `component` (STAGE-O5 resolved) |

The scope names and their order are frozen
([DESIGN_POLICY.md §15](DESIGN_POLICY.md#15-decisions-frozen-early)). Blender
scene names, collection names and datablock names never change this top-level
structure; they appear only below a scope.

`geo` and `mtl` are always authored, even empty, so a consumer can rely on
them. The other scopes are authored only when they have content.

Objects are placed by the type of their data: mesh, curve, point-cloud and
empty objects under `geo`, armatures under `skel`, cameras under `cameras`,
lights under `lights`. Object parenting is preserved as prim nesting inside a
scope. A parent relation that crosses scopes is STAGE-O4.

## 5. Layer metadata

Every layer authored by `usdBlendFileFormat` explicitly sets `upAxis = "Y"`
and `metersPerUnit = 1` (one USD unit is one meter). These are fixed output
settings, including for the minimal Phase 0 stage, not USD defaults or values
inherited from the source Blender scene. Source axis and unit conversion are
handled separately in §6.

```usda
#usda 1.0
(
    defaultPrim = "Asset"
    upAxis = "Y"
    metersPerUnit = 1
)
```

When the scene has animation (Phase 5), the layer also carries
`startTimeCode`, `endTimeCode`, `timeCodesPerSecond` and `framesPerSecond`
(§15). Nothing time- or host-dependent is authored: no timestamps, no
absolute source paths, no user or machine names.

`/Asset` customData carries provenance:

| Key | Type | Meaning |
| --- | --- | --- |
| `blend:stageContractVersion` | `int` | §2 |
| `blend:sourceVersion` | `string` | the Blender version that saved the file, e.g. `"4.5"` |
| `blend:sourceScene` | `string` | the source name of the authored scene |

## 6. Coordinate conversion

Blender is right-handed, **Z-up**, with front facing −Y. The stage is
right-handed, **Y-up**, with front facing +Z. One rotation converts between
them, `C` = −90° about X:

```text
(x, y, z)_blender  →  (x, z, −y)_usd
```

The conversion happens once, in `blendScene`, while the Scene IR is built
([DESIGN_POLICY.md §5.2](DESIGN_POLICY.md#52-blendscene--the-scene-ir-and-native-decoding)).
Authoring receives values already in the USD basis and never converts again.

Object matrices convert by their data's local convention:

| Object data | World matrix | Local data |
| --- | --- | --- |
| mesh, curves, points, empty, armature | `W_usd = C · W_blender · C⁻¹` | converted by `C` |
| camera, light | `W_usd = C · W_blender` | unchanged: Blender and USD both look and emit along local −Z with +Y up |

Local transforms are recomputed from the converted world matrices of the prim
and its authored parent, so both rows compose under any parent.

### 6.1 Scene units

The selected unit policy is physical-scale normalization: `blendFile` reads
stored values unchanged, `blendScene` converts distances to meters and the USD
basis, and `usdBlendFileFormat` authors those values unchanged with
`metersPerUnit = 1`. Unit conversion happens exactly once, before values enter
the Scene IR; neither the output layer metadata nor a root Xform applies the
source scale again.

The conversion factor is `S = Scene.unit_settings.scale_length` from the
selected active Scene, not from an arbitrary Scene datablock or host
preferences. A distance becomes `value * S`; a position follows the canonical
order `C * (S * position)`. For an affine mesh/empty world matrix, multiply only
the source translation by `S`, then apply the basis conjugation above. Rotation,
dimensionless object scale, shear and negative scales are not unit-scaled.
Convert world matrices first, then reconstruct parent-relative transforms
from those converted matrices. Mesh points and transform translations must
use the same conversion factor.

| Quantity | Unit treatment |
| --- | --- |
| mesh/curve/point positions, physical radii and widths | meters before entering the IR |
| object, bone, rest-pose and animated translations | meters before entering the IR |
| light radius and area dimensions | meters; intensity remains a separate STAGE-O6 decision |
| camera clipping/focus distances and scene-space orthographic size | distance normalization; property mapping must be fixture-backed |
| camera focal length and aperture | separate optical-unit mapping, STAGE-O7; never blindly multiply by `S` |
| normals and directions | basis rotation only, no unit scaling |
| object scale, angles, UVs, colors, indices, weights and normalized parameters | no unit scaling |
| future physics quantities | explicit dimensional mapping: length `S`, area `S^2`, volume `S^3`; mass/force/density are not blanket-scaled |

`extent` is computed from already-normalized meter-space mesh points. The
factor is scene metadata, not an animation channel. Unit-system labels such
as metric, imperial or none describe source presentation: they must not add
hard-coded factors on top of `scale_length`.

The original `sourceUnitScale` is provenance only. Future `/Asset` customData
uses `blend:sourceUnitScale` (`double`) and `blend:sourceUnitSystem` (`string`)
once scene decoding and the source-system mapping are fixture-backed. These
are not yet required or authored by the header-only importer, and do not
change the interpretation of IR or USD geometry.

The scale must be finite and strictly positive. Zero, negative, NaN or infinite
values fail scene decoding with `BLEND_SCENE_UNIT_SCALE_INVALID`, with no
guessed `1.0` fallback. Nonfinite source distances and meter-conversion
overflow also fail, rather than publishing invalid geometry. The current
[IR helpers](DESIGN_POLICY.md#521-scene-ir-foundation) enforce this boundary;
they do not read a saved Scene or author USD.

STAGE-O1 remains open for source-field and end-to-end evidence, not for a
choice between preserving Blender numbers and meter normalization. Before
freezing it, Blender-written `unit-1m.blend`, `unit-1cm.blend`,
`unit-1mm.blend` and `unit-10m.blend` must cover scales `1`, `0.01`, `0.001`
and `10`, each with a cube, a translated object and a parent/child pair.
Equivalent physical cubes (1, 100, 1000 and 0.1 Blender units wide) must
produce matching meter-space points and world dimensions, with every authored
stage still declaring `metersPerUnit = 1`. Camera/light evidence belongs to
their later schema mappings; synthetic arithmetic alone does not prove saved
Blender field semantics or authored-stage support.

## 7. Objects and transforms

Each object is a `UsdGeomXform` named after the object
([NAMING_POLICY.md](NAMING_POLICY.md)). Its data is a child prim with a fixed
name, so object transform and datablock stay separate:

| Data | Child |
| --- | --- |
| mesh | `mesh` (`UsdGeomMesh`) |
| curves | `curves` (`UsdGeomBasisCurves` or `UsdGeomNurbsCurves`) |
| point cloud | `points` (`UsdGeomPoints`) |
| empty | none |

The object's local transform is authored as one `xformOp:transform`
(`matrix4d`), with `xformOpOrder = ["xformOp:transform"]`. Decomposition into
translate, orient and scale is deferred until a consumer needs it.

Use `blendScene`'s
[parent-relative helper](DESIGN_POLICY.md#521-scene-ir-foundation) on the
converted world matrices of the Object and its authored parent; an IR root
uses identity as its parent. Transpose the resulting column-vector matrix for
OpenUSD's row-vector convention, without a second unit or basis conversion.
A singular or numerically singular parent fails explicitly with
`BLEND_SCENE_TRANSFORM_SINGULAR`, not by silently detaching the child or
substituting an identity matrix. A zero-scale root or child is not itself an
error unless its world matrix must be inverted as a parent. The helper has
native/oracle evidence; USD Xform authoring remains unimplemented.

Objects hidden for rendering (`hide_render`) are authored with
`visibility = "invisible"`. Objects with no data and no children are still
authored, as empties.

Several objects can share one mesh datablock. Phase 2 authors the mesh under
each object; sharing it is STAGE-O2.

## 8. Meshes

`UsdGeomMesh`, from the mesh's **source** data — no modifier is applied
([DESIGN_POLICY.md §2.3](DESIGN_POLICY.md#23-source-data-not-evaluated-data)).

| Blender | USD | Notes |
| --- | --- | --- |
| vertex positions | `points` | meters in the USD basis (§6) |
| face sizes | `faceVertexCounts` | |
| face-corner vertex indices | `faceVertexIndices` | Blender's winding; `orientation = "rightHanded"` |
| — | `subdivisionScheme = "none"` | Blender meshes are polygonal; a Subdivision Surface modifier is not applied |
| normals | `normals`, `faceVarying` | sharp edges and custom split normals preserved |
| active render UV map | `primvars:st`, `texCoord2f[]`, `faceVarying`, indexed | Blender's UV origin is USD's; no flip |
| other UV maps | `primvars:<uvName>`, `texCoord2f[]`, `faceVarying`, indexed | |
| material slots | `UsdGeomSubset` per used slot, `familyName = "materialBind"`, `elementType = "face"` | [MATERIAL_POLICY.md §7](MATERIAL_POLICY.md#7-binding) |
| color attributes | `primvars:<name>` (STAGE-O3) | Phase 3 or later |
| generic attributes | `primvars:<name>` where the domain maps | later |

`extent` is authored from the converted points. An empty mesh is authored
with empty arrays and a diagnostic.

## 9. Curves and points

| Blender | USD |
| --- | --- |
| poly and Bézier curves | `UsdGeomBasisCurves` (`linear`, `bezier`) |
| NURBS curves | `UsdGeomNurbsCurves` |
| hair curves (`Curves` datablock) | `UsdGeomBasisCurves` |
| point cloud | `UsdGeomPoints` |

Not scheduled by a Phase yet; recorded so the layout is known. Curve
geometry produced by Geometry Nodes is evaluated data and is not read by the
native backend.

## 10. Materials

Every material is a `UsdShadeMaterial` under `/Asset/mtl`, whatever datablock
or slot it came from, and every binding targets `/Asset/mtl`. The layout below
each material is [MATERIAL_POLICY.md §3](MATERIAL_POLICY.md#3-hierarchy).

## 11. Cameras

Each camera object is a `UsdGeomCamera` under `/Asset/cameras`.

| Blender | USD |
| --- | --- |
| type `PERSP` | `projection = "perspective"` |
| type `ORTHO` | `projection = "orthographic"`; aperture from `ortho_scale` |
| type `PANO` | authored as perspective with a diagnostic |
| `lens` | `focalLength` |
| sensor width and height, sensor fit, render aspect | `horizontalAperture`, `verticalAperture` |
| `shift_x`, `shift_y` | `horizontalApertureOffset`, `verticalApertureOffset` |
| `clip_start`, `clip_end` | `clippingRange` |
| depth of field focus distance | `focusDistance` |
| depth of field f-stop, when enabled | `fStop` |

Lens and aperture units are STAGE-O7. Every Blender–USD camera difference is
handled in one converter in `blendScene`, and fixed by fixtures.

## 12. Lights

Each light object is a `UsdLux` light under `/Asset/lights`.

| Blender | USD |
| --- | --- |
| `POINT` | `UsdLuxSphereLight`, `radius` from the light's radius |
| `SUN` | `UsdLuxDistantLight`, `angle` from the sun's angular diameter |
| `SPOT` | `UsdLuxSphereLight` + `UsdLuxShapingAPI`, cone angle and softness from spot size and blend |
| `AREA` square, rectangle | `UsdLuxRectLight` |
| `AREA` disk | `UsdLuxDiskLight` |
| `AREA` ellipse | `UsdLuxDiskLight`, approximated, with a diagnostic |

Color maps to `inputs:color`. The energy, exposure and intensity conversion is
STAGE-O6, and is fixed by fixtures before Phase 4 is done.

## 13. Collections

Blender collections do not form a transform hierarchy: an object can be in
several, and the prim hierarchy follows object parenting (§4). Collections are
grouping metadata.

`/Asset/collections` has `UsdCollectionAPI` applied once per Blender
collection, with the collection's identifier as the instance name. Each
instance includes its objects' prims, and its child collections by their
collection paths (`/Asset/collections.collection:<child>`). The scene's master
collection is not authored. Collection instancing (an empty that instances a
collection) is later.

## 14. Skeleton and skinning

Phase 6. Each armature is a `UsdSkelSkeleton` under `/Asset/skel`, with joints
in a deterministic order: parents before children, siblings in the order the
armature stores them. Skinned meshes get `UsdSkelBindingAPI`, with joint
indices and weights from vertex groups that name bones, and armature
animation becomes `UsdSkelAnimation` (§15).

`UsdSkel` only skins geometry below a `UsdSkelRoot`, and skinned meshes live
under `/Asset/geo`. Where the root is is STAGE-O8.

## 15. Animation and time

Phase 5 onward.

| Blender | USD |
| --- | --- |
| scene `fps / fps_base` | `timeCodesPerSecond`, `framesPerSecond` |
| scene frame start and end | `startTimeCode`, `endTimeCode` |
| frame number | time code (frame *n* is time code *n*) |

Transform animation from Actions and F-Curves is sampled, at every integer
frame of the scene range, into time samples on `xformOp:transform`. USD
interpolates linearly between samples, so Blender's curve interpolation is
reproduced by sampling, not by authoring curves. Sub-frame sampling is later.

Armature animation becomes `UsdSkelAnimation` under the skeleton, sampled the
same way. Shape keys are investigated in Phase 6. Constraints, drivers and
modifier animation are evaluated data
([DESIGN_POLICY.md §8](DESIGN_POLICY.md#8-animation-and-skinning-policy)).

## 16. Metadata-only layers

When `Read` is called with `metadataOnly = true`, the layer holds:

- the layer metadata of §5;
- `/Asset` with its customData, and the scopes of §4;
- every object prim, with its type, identifier and transform.

It does not hold mesh, curve or point data, material networks, texture
references, or animation samples. The data child prims (`mesh`, …) are
authored with their type and no attributes, so the hierarchy is the same as
the full read.

## 17. Validation checklist

Every fixture that opens through the registered plugin is checked for:

1. `defaultPrim` is `Asset`; `/Asset` is a `UsdGeomXform` (or STAGE-O8's root).
2. `upAxis` is `Y`; `metersPerUnit` is `1`.
3. `/Asset/geo` and `/Asset/mtl` exist and are `UsdGeomScope`.
4. `/Asset.customData["blend:stageContractVersion"]` is the current version.
5. Every authored prim is in a scope of §4; no other child of `/Asset` exists.
6. Every object prim is a `UsdGeomXform` with exactly the transform op of §7.
7. Every material binding targets a prim under `/Asset/mtl`.
8. Every `UsdGeomMesh` has `subdivisionScheme = "none"` and valid topology.
9. Every prim name is a valid identifier, and opening the file twice authors
   identical layers.
10. No absolute local path appears in the layer.

## 18. Open questions

Identifiers are never reused. The [roadmap](../roadmap/README.md#open-decisions)
schedules them.

| Id | Question | Proposed answer | Blocks |
| --- | --- | --- | --- |
| STAGE-O1 | Fixture-backed semantics of the active scene's `unit_settings.scale_length`. | Apply once in `blendScene`: distances and translations become meters, dimensionless values do not scale, and USD keeps `metersPerUnit = 1`. Freeze after the Blender-written equivalence tests in §6.1. | Phase 2 |
| STAGE-O2 | How is a mesh shared by several objects authored? | Phase 2 duplicates it per object. Later: one prototype and references or instancing, once a fixture shows the cost. | nothing (non-blocking) |
| STAGE-O3 | Do color attributes become `primvars:displayColor`? | The active render color attribute also becomes `displayColor`; every color attribute is `primvars:<name>`. | Phase 3 |
| STAGE-O4 | Where does an object go whose parent is in another scope (a camera parented to a mesh)? | In its own scope, with the transform relative to `/Asset`, and the source parent recorded in customData. Animated parents then need baked samples. | Phase 4 |
| STAGE-O6 | The light intensity conversion (watts, irradiance, exposure). | Fixed per light type by fixtures rendered in both Blender and a USD renderer. | Phase 4 |
| STAGE-O7 | Camera lens and aperture units with `metersPerUnit = 1`. | Millimeters, as Blender stores them and as most USD tools read them; confirm against the OpenUSD release in use. | Phase 4 |
| STAGE-O8 | Where is the `UsdSkelRoot` for skinned meshes under `/Asset/geo`? | `/Asset` becomes the `UsdSkelRoot` when the scene has a skinned mesh, as `usd-mmd-plugins` does. | Phase 6 |

### 18.1 Resolved decisions

- **STAGE-O5 (2026-10-01):** `/Asset` has kind `component`, matching the
  sibling asset contracts. `test_stage.py` verifies this value.

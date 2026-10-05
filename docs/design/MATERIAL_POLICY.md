# Material policy

> Status: **proposed**, 2026-10-01. This document defines intended behavior;
> implemented behavior belongs to the
> [capability matrix](../reference/CAPABILITY_MATRIX.md). Phase scope belongs
> to [DESIGN_POLICY.md §14](DESIGN_POLICY.md#14-phases).
>
> This document owns how Blender materials, their node trees and their images
> become USD. On this area it wins over the design policy.

## 1. Scope

Blender materials are node graphs evaluated by Cycles and EEVEE. Reproducing
them completely is not a goal of the first releases. The first goal is that a
generic USD renderer shows a `.blend` with the right base colors, textures,
transparency and surface detail.

## 2. Principles

1. **Standard shading first.** `UsdPreviewSurface`, `UsdUVTexture` and
   `UsdPrimvarReader_float2` carry the first realization.
2. **No Blender node schemas.** Blender node types are never mirrored as USD
   schemas or as custom shader IDs.
3. **Two paths, one material.** A simple compatibility path now, a
   high-fidelity MaterialX path later (§8), both below the same
   `UsdShadeMaterial`:

```text
Blender nodes
   ├─ compatibility path → UsdPreviewSurface   (Phase 3)
   └─ high-fidelity path → MaterialX            (later)
```

4. **Never claim exactness.** A material whose graph is outside the supported
   subset (§5) is authored as the closest constant fallback, with a
   diagnostic.

## 3. Hierarchy

```text
/Asset/mtl/<Material>             UsdShadeMaterial
   outputs:surface  → preview.outputs:surface
   └─ preview                     UsdShadeNodeGraph
      outputs:surface → Surface.outputs:surface
      ├─ Surface                  UsdPreviewSurface
      ├─ <Texture>                UsdUVTexture, one per used image input
      └─ StReader                 UsdPrimvarReader_float2, varname "st" (or the node's UV map)
```

Every material lives directly under `/Asset/mtl`, named after the material
([NAMING_POLICY.md](NAMING_POLICY.md)), whatever datablock or slot it came
from. The realization name `preview` is fixed; `mtlx` is reserved for §8.
Shader prim names inside `preview` are derived from the input they feed
(`BaseColorTexture`, `NormalTexture`, …), not from Blender node names, so they
are stable when a user renames a node.

The Material connects through the NodeGraph's `outputs:surface`, not directly
to an internal shader. `Surface` has shader ID `UsdPreviewSurface`; nesting is
a standard USD NodeGraph boundary, not a Blender-specific schema.

## 4. Principled BSDF mapping

The supported surface is a **Principled BSDF** connected to the active
**Material Output**'s Surface socket.

| Principled BSDF input | `UsdPreviewSurface` input | Notes |
| --- | --- | --- |
| Base Color | `diffuseColor` | |
| Metallic | `metallic` | |
| Roughness | `roughness` | |
| IOR | `ior` | |
| Alpha | `opacity` | the mode is MAT-O1 |
| Normal (through a Normal Map node) | `normal` | tangent space; texture `scale = (2,2,2,1)`, `bias = (-1,-1,-1,0)`; Normal Map strength other than 1 is a diagnostic |
| Emission Color × Emission Strength | `emissiveColor` | the strength scale is MAT-O4 |
| Coat Weight | `clearcoat` | |
| Coat Roughness | `clearcoatRoughness` | |

`useSpecularWorkflow = 0` always. Inputs not in the table (subsurface,
sheen, transmission, anisotropy, …) are not authored; a non-default value is
reported with a diagnostic.

A material that uses no nodes is authored from its viewport values: diffuse
color, metallic and roughness.

## 5. Supported node subset

An input of the Principled BSDF is authored as:

| Linked from | Authored as |
| --- | --- |
| nothing | the socket's constant value |
| Image Texture (Color or Alpha output) | `UsdUVTexture` reading the image (§6), connected to the input |
| Image Texture → Normal Map → Normal | `UsdUVTexture` with the bias and scale of §4 |
| UV Map node into an Image Texture's Vector | the texture's `StReader` reads that UV map's primvar |
| anything else | the socket's constant value, with `BLEND_MATERIAL_UNSUPPORTED_NODE` |

Image Texture settings:

| Blender | `UsdUVTexture` |
| --- | --- |
| color space sRGB | `sourceColorSpace = "sRGB"` |
| color space Non-Color | `sourceColorSpace = "raw"` |
| extension Repeat | `wrapS`, `wrapT` = `repeat` |
| extension Extend | `clamp` |
| extension Clip | `black` |
| extension Mirror | `mirror` |
| interpolation Closest | a diagnostic; USD has no filter input |

## 6. Images and paths

| Image source | Phase 3 |
| --- | --- |
| external file, relative (`//textures/a.png`) | asset path relative to the `.blend`: `./textures/a.png` |
| external file, absolute | the path as stored, with `BLEND_IMAGE_ABSOLUTE_PATH`; never rewritten to a machine-local guess |
| packed into the `.blend` | not authored as a texture; `BLEND_IMAGE_PACKED` (MAT-O3) |
| generated, movie, sequence | not authored; a diagnostic |
| UDIM tiles | `<UDIM>` in the asset path, later |

Blender's `//` prefix means "relative to the `.blend`". Separators are written
as `/`. Path rules shared with other names are
[NAMING_POLICY.md §6](NAMING_POLICY.md#6-asset-paths).

Serving packed images needs a virtual asset identity such as
`blend://path/to/file.blend#image/<id>`. That is an asset resolver or package
resolver concern, not the file format's, and is not taken on early
([DESIGN_POLICY.md §9](DESIGN_POLICY.md#9-asset-resolution-boundary)).

## 7. Binding

- A Blender material slot links its material either to the object or to the
  mesh. `blendScene` resolves the effective material per slot, so authoring
  sees one list per object.
- A mesh using one slot binds its material on the `mesh` prim with
  `UsdShadeMaterialBindingAPI`.
- A mesh using several slots gets one `UsdGeomSubset` per used slot, with
  `familyName = "materialBind"`, `elementType = "face"`, and the subset's
  material bound to it.
- An empty slot, or faces whose material index has no slot, get no binding,
  and a diagnostic.
- Every binding target is under `/Asset/mtl`
  ([STAGE_CONTRACT.md §10](STAGE_CONTRACT.md#10-materials)).

### 7.1 Native constant-material boundary

The owning Scene IR carries `Scene.materials`, constant `Material` values,
`Object.materialSlots` (optional indices into that material vector), and
`Mesh.faceMaterialIndices`. Slots are resolved independently for each Object,
even when several Objects share one Mesh. A missing face-index attribute means
slot zero; a Mesh with no slots is normally unbound. A nonempty IR face-index
vector must contain exactly one index per polygon.

Saved Mesh and Object slot counts must agree and be nonnegative. Material
pointer arrays and Object ownership bytes are exact-length raw DATA blocks;
ownership is zero for Mesh data and one for Object data. A null Object override
stays null, rather than falling back to the Mesh's slot. Nonzero references
must resolve exactly to Material IDs; linked Materials are diagnosed and not
followed. Only Materials reached through effective slots enter this boundary.

For Blender 5.x node constants, select the active all-renderers Material Output
and a direct Principled BSDF Surface link by saved node type and socket
identifier, never by editable node names. Unsupported, muted, renderer-specific
or absent surface graphs use viewport constants with a diagnostic. Linked
Principled inputs outside §7.2 use saved socket constants with a diagnostic.
Muted links do not enter the effective graph; invalid links use diagnosed
viewport fallback.
Non-default unsupported inputs and deferred Alpha/Emission are reported.
Older node-tree layouts use diagnosed viewport fallback; this is not expanded
older-version compatibility.

Graph lists are iterative and bounded by the supplied block sequence. Their
endpoints, backlinks, cycles, socket ownership and link endpoints are checked.
Typed defaults must have the required shapes and finite float values.
Malformed references, arrays, lists and values fail without a partial Scene.
Schemas with no Material definition expose no material feature only when
there are no Material IDs or nonempty slots; otherwise decoding fails.

One valid slot binds on the Mesh only if every face index is valid. Otherwise
only valid, nonempty used slots get `material_<slot>` face subsets; invalid or
empty-slot faces remain unbound. The `materialBind` family is `nonOverlapping`,
not `partition`, so incomplete coverage is legal. All subset and whole-Mesh
bindings use `UsdShadeMaterialBindingAPI`. Material names and ordering follow
[NAMING §4.3](NAMING_POLICY.md#43-material-naming-boundary).

### 7.2 Native external-texture boundary

The owning `Material.textures` vector carries the destination input, normalized
asset path, sRGB/raw color space, wrapping mode and raw UV-map selections.
Native decoding does not open images, read pixels or allocate USD identifiers.
The existing constant values remain available as fallbacks. Image and graph
storage is validated through SDNA and exact saved references; malformed
references, typed storage, lists and strings fail without a partial Scene.
Linked Images are diagnosed without following their Library; packed-image list
framing is checked, but packed payloads are not decoded.

This is a bounded step toward §5, not completion of that intended subset:

- Image Texture **Color → Base Color** and **Alpha → Metallic, Roughness,
  IOR, Coat Weight or Coat Roughness** are translated. Color-to-scalar and
  Alpha-to-color coercions use diagnosed constants until their Blender
  semantics can be reproduced without choosing an arbitrary channel.
- Normal requires **Image Texture Color → Normal Map → Normal**, tangent
  space, OpenGL convention, displaced base, constant strength one and an
  unmuted graph. Other strengths/spaces/conventions/bases use diagnosed
  geometric-normal fallback.
- Image Texture projection is Flat. Vector is unlinked or comes directly from
  an unmuted UV Map node without instancer coordinates. Mapping and other
  upstream nodes use diagnosed constants. Embedded texture/color mapping
  controls must retain their identity/default settings; non-default transforms,
  limits, projections, color adjustments or ramps are diagnosed, not ignored.
- Only external FILE Images with sRGB or Non-Color and Straight alpha are translated.
  Packed/generated/movie/sequence/tiled/linked/missing images and other color
  spaces/alpha interpretations are diagnosed and omitted. Linear interpolation is the direct
  subset; other interpolation is diagnosed while retaining the texture.
- Blender-relative `//` paths become `./` before separator normalization.
  Stored drive, rooted and backslash-UNC paths are normalized to `/` and
  diagnosed, never probed or machine-locally rewritten. Empty paths are missing
  images; paths that are neither Blender-relative nor absolute are diagnosed
  and omitted. Missing external files remain the asset resolver's concern.

Authoring keeps all shaders inside `preview`, with one `<Input>Texture` per
destination and standard shader IDs only. Active-render UVs use `StReader`
with `varname = "st"`; a named non-render map uses `<Input>StReader` and the
actual [UV identifier](NAMING_POLICY.md#42-uv-map-naming-boundary), including
collision suffixes. Selecting the render map by its source name still reads
`st`; no UV aliases are introduced.

A shared Material requires the selected UV to exist and have the same authored
identifier on every effectively bound Mesh. Normal Map's tangent UV selection
must resolve to that same identifier. Missing, inconsistent or mismatched UVs
produce `BLEND_MATERIAL_UV_UNSUPPORTED` and omit that input's texture, retaining
constants or geometric normals. This avoids silently binding a reader to an
unrelated primvar; per-Mesh Material specializations are not introduced.

Texture input order is canonical, independent of the IR vector and saved block
order. Normal textures use §4's scale/bias and a flat encoded-normal fallback
`(0.5, 0.5, 1, 1)`; other texture fallbacks retain the socket constants.
Metadata-only reads retain typed texture/reader children without shader IDs,
inputs, asset references or connections. Alpha/Emission remain deferred.
The [5.2.2 fixture record](../../tests/fixtures/native-textures/README.md)
owns provenance and limitations; shader-network evidence is not renderer
equivalence or release-wide Blender compatibility.

## 8. MaterialX

A later path translates a wider set of Blender nodes into a MaterialX graph
under `/Asset/mtl/<Material>/mtlx`, beside `preview`. The target node set
(`standard_surface` or `open_pbr_surface`) and the supported Blender nodes are
MAT-O2. It is not scheduled by a Phase yet.

## 9. Open questions

| Id | Question | Proposed answer | Blocks |
| --- | --- | --- | --- |
| MAT-O1 | How does Blender's alpha (blend, clip, hashed; and the 4.2+ render method) map to `opacity` and `opacityThreshold`? | Clip → `opacityThreshold` from the clip value; everything else → `opacity` with threshold 0. Confirm against 4.5 and 5.x fixtures. | Phase 3 |
| MAT-O2 | The MaterialX target node set and the Blender nodes it covers. | `open_pbr_surface` if the OpenUSD release in use renders it in Storm; otherwise `standard_surface`. | nothing (non-blocking) |
| MAT-O3 | Packed images. | Report only, until a resolver serves them (§6). | nothing (non-blocking) |
| MAT-O4 | Emission strength units against `emissiveColor`. | Multiply color by strength; confirm visually against a fixture. | Phase 3 |

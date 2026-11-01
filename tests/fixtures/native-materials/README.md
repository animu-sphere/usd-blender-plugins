# Native constant-material fixtures

This repository's [generate_materials.py](../generate_materials.py) writes
these original, redistributable test scenes under the repository
[license](../../../LICENSE). No Blender source or external asset is copied.
Blender is a separate generation/oracle process, not a native test dependency.

## Provenance

Generated and checked on Windows on 2026-10-05 with Blender 5.2.2 LTS,
build `d13f752e3b9c`. `blender-5.2.2/materials.blend` is an uncompressed
normal save, 705379 bytes, SHA-256
`700b688213dda218536262b450ddba7773c237061456d0308e1bab483998565d`.
File-browser directories are normalized to `//`.

The active `Materials` Scene has ten Objects, nine Meshes and eight
effective Materials:

- `Single` binds one constant Material; source `A/B` collides with `A_B`.
- `Multi` uses two slots over face indices `[0, 1, 1, 0]`. Its Principled
  shader has a renamed node and an inactive alternative Material Output.
- `Override` shares `MultiData`, overriding slot zero with `Linked` and
  slot one with null. It must not inherit the Mesh's second Material.
- `EmptySlot` uses `[0, 1, 2, 1]` with an empty middle slot; `Unbound` has
  no slots.
- `Linked` feeds Principled Base Color through RGB, outside the constant
  subset; the socket's saved color is the diagnosed approximation.
- `Fallback` uses Diffuse BSDF, `Muted` mutes Principled and `MutedLink`
  mutes the Surface link; all use diagnosed saved viewport constants.
- `Unsupported` has nonzero Transmission Weight. `Deferred` has Alpha
  0.25 and white Emission Color with Strength 3; Alpha is diagnosed and
  Emission is authored as `(3, 3, 3)`.

Blender 5.2.2 creates node trees even when its deprecated `use_nodes` setter
receives false. The non-node decoder branch is therefore tested with a saved
flag mutation, not claimed as a Blender-written non-node fixture.

## Oracle and checks

The adjacent `materials.oracle.json` records Blender's saved version/Scene,
constant inputs including Emission Color and Strength or viewport fallback, expected diagnostic families,
effective Object slots, shared Mesh names and exact face material indices.
Generation checks the oracle again after reopening the saved file.
`--check` regenerates only in a temporary directory and then reopens the
committed file; neither the fixture nor its oracle is rewritten. Normal-save
bytes and saved addresses are not claimed reproducible.

[test_generate_materials.py](../test_generate_materials.py) checks
independent-process/different-path semantic reproduction, home-path exclusion,
non-destructive success/failure, changed-oracle rejection and saved constant,
Emission Color and Strength,
Object-slot and face-index mutations.

Its Blender 5.2.2 Cycles CPU render test also reopens `Deferred`, confirms the
saved Alpha 0.25 and Emission Strength 3, then varies those inputs and sets
Emission Color to `(0.25, 0.5, 0.75)`. With a black Base Color and transparent
film, it checks opaque strengths 0, 1 and 3 and alpha 0.25 and 0 at strength 3.
The average 8×8 OpenEXR pixels have alpha equal to the socket value and
premultiplied RGB equal to color × strength × alpha, within 0.02 and 0.03
respectively. This is source-render evidence for those cases, not a USD renderer
comparison, render-method/threshold decision, or implementation claim.

The separate [Eevee test](../test_material_alpha_eevee.py) checks Blender 5.2.2
on Windows with GPU rendering. Starting from `DITHERED`, the deprecated
`blend_method` setter maps `OPAQUE`, `CLIP` and `HASHED` to `DITHERED`, and
`BLEND` to `BLENDED`. It also saves and reopens temporary fixture variations
for both current render methods, thresholds 0.1 and 0.9 and constant Alpha
0.25 and 0.75. With transparent film and the same black-base, emissive plane,
8×8, 256-sample OpenEXR averages remain within 0.02 of the input Alpha and
0.03 of premultiplied emission, even when the threshold crosses Alpha.
The committed fixture is not modified. These are bounded source-render cases,
not evidence about every material graph, Blender version or USD renderer.

The registered-plugin [Storm check](../../../docs/guides/building.md#direct-emission-rendering-in-storm)
uses this same saved `Deferred` binding and compares visible emission against
session-only zero and non-white strength controls. It is not a linear-pixel
comparison with Cycles, and the fixture's Alpha remains deferred.

[MaterialTests.cpp](../../../libs/blendScene/tests/MaterialTests.cpp) checks
native constants, non-white and linked Emission mutations, effective slots,
shared Mesh identity, exact face indices, contextual diagnostics and malformed
saved storage. The `usdBlend.materials`
CTest checks USD authoring, material/block permutations, invalid IR and
partial invalid-face recovery. Registered
[test_stage.py](../../../plugins/usdBlendFileFormat/tests/test_stage.py)
compares the JSON oracle to shader values and exact binding/subset targets,
including reference remapping and metadata-only hierarchy without networks
or bindings. Commands are in the
[build guide](../../../docs/guides/building.md#constant-material-oracles).

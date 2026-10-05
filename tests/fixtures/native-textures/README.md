# Native external-texture fixtures

This repository's [generate_textures.py](../generate_textures.py) creates
these original test scenes and the four-pixel PNG under the repository
[license](../../../LICENSE). No external artwork or Blender source is copied.
Blender is a separate generation/oracle process, not a native test dependency.

## Provenance

Generated and checked on Windows on 2026-10-05 with Blender 5.2.2 LTS,
build `d13f752e3b9c`. `blender-5.2.2/textures.blend` is an uncompressed normal
save, 1403352 bytes, SHA-256
`b503ff5881272b1fc88a5831666c6bad27d9f8d3f91e86045d8a31f0ced66588`.
File-browser directories are normalized to `//`. Packed-image paths also use
the fixture-relative identity; generated saves contain no home-directory paths.

The active `Textures` Scene has 27 independent quad Objects/Meshes, each bound
to one Material. Every Mesh has `Detail/UV`, `Detail_UV` and active-render
`Render` UV maps. Their USD names are `Detail_UV`, `Detail_UV_1` and `st`.
The Image and Principled nodes are renamed to prove saved type/socket selection
rather than user-editable-name matching.

Supported network cases:

- `Relative`: `//textures/color.png`, default render UV, sRGB, Repeat.
- `NamedUV`: `Detail_UV`, Extend/clamp; `ActiveUV`: explicit `Render`, Clip/black.
- `Mirror`: Mirror wrapping; `Raw`: Non-Color/raw.
- `Absolute`: stored `C:\usd-blend-fixtures\color.png`; `UNC`: stored
  `\\usd-blend-fixtures\textures\color.png`. These are deterministic test
  identities, not files that should exist on the reader's machine.
- `AlphaScalar`, `MetallicAlpha`, `IorAlpha`, `ClearcoatAlpha`,
  `ClearcoatRoughnessAlpha`: Alpha feeds Roughness, Metallic, IOR, Coat Weight
  and Coat Roughness respectively.
- `Normal` and `NamedNormal`: Non-Color Image Color through an OpenGL,
  displaced-base, strength-one tangent Normal Map, using matching default or
  named texture/tangent UV selections.
- `Closest`: retains its texture with an unsupported-interpolation diagnostic.

Diagnosed fallback cases are Packed, Generated, Movie, Sequence, Tiled,
Missing Image, Box projection, muted Image Texture, Mapping-to-Vector,
Color-to-Roughness coercion, non-unit normal strength and Object-space normal
mapping. Constants/geometric normals remain; unsupported images are not
extracted or decoded.

## Oracle and checks

The adjacent `textures.oracle.json` records saved Scene/version, constant
fallbacks, supported shader inputs, path/color-space/wrap settings, actual
UV-reader primvars, expected normal decoding and diagnostic families.
Generation checks these values again after reopening the saved file.
`--check` regenerates only in a temporary directory, then reopens the committed
file without rewriting its bytes, oracle or PNG. Normal-save addresses and
whole-file bytes are not claimed reproducible.

[test_generate_textures.py](../test_generate_textures.py) verifies independent
process/path semantic reproduction, home-path exclusion, non-destructive
success/failure, changed-oracle rejection and saved path, wrapping, UV,
normal-strength, Image and normal-space mutations.

[MaterialTests.cpp](../../../libs/blendScene/tests/MaterialTests.cpp),
run as `blendScene.textures`, checks owning native values, block-order
independence, contextual warnings and malformed Image/node/storage/list/path
references. Additional saved mutations cover nonfinite strength, unsupported
color space, non-Straight alpha, linked Image, ambiguous paths, embedded
texture/color mapping, nonfinite mapping scale and normal convention/base.

[AuthorSceneTests.cpp](../../../plugins/usdBlendFileFormat/tests/AuthorSceneTests.cpp),
run as `usdBlend.textures`, checks standard texture/reader shader IDs and
normal scale/bias/fallback, UV collision mapping, metadata-only typed hierarchy,
block/material/texture-vector determinism, multiple inputs sharing `st`,
invalid IR and missing/shared-Material/tangent UV recovery.
Both root and standalone builds run these tests without Blender.

Registered [test_stage.py](../../../plugins/usdBlendFileFormat/tests/test_stage.py)
compares every network case to the JSON oracle, checks exact connections,
bindings, texture/reader inputs, relative-asset resolution, repeated and
referenced reads, metadata hierarchy and exact full/metadata diagnostic
families with Material byte/block/name context.
Commands are in the
[build guide](../../../docs/guides/building.md#external-texture-and-normal-map-oracles).

Local Windows verification on 2026-10-05 passed both builds, all 19 root Scene
CTests, all six root USD CTests, the standalone native texture CTest, all six
standalone USD CTests, all 12 registered stage tests and both generator
regression tests. C++ formatting, Python syntax and patch whitespace checks
also passed. These are local results, not hosted Windows/Linux or rendering
acceptance.

## Limitations

This is 64-bit little-endian Blender 5.2.2 storage/network evidence, not
release-wide Blender 5.x certification or full older-version support.
No renderer is exercised here: named-UV tangent-frame and filtering equivalence
still need generic-renderer acceptance. Color/scalar cross-type coercions,
material Alpha/Emission, nonstandard color spaces/alpha interpretations,
non-default embedded texture/color mappings, image extraction, UDIM,
linked assets and richer shader graphs are not claimed supported.
The decoder does not probe whether external files exist; the registered test
separately proves resolution of the committed relative PNG.

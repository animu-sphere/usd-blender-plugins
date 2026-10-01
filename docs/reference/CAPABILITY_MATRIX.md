# Capability matrix

This page is the **only** document that says what is implemented. Every other
document describes intended behavior and links here.

As of 2026-10-01 Phase 0 is in progress. Only header validation and minimal
stage scaffolding are implemented; no Blender scene version is supported yet.
Synthetic fixtures below live in `plugins/usdBlendFileFormat/tests/fixtures/`;
the contributor-provided Blender file lives in `tests/corpus/` beside them.
Header validation does not establish scene compatibility. Windows verification is recorded
in [the build guide](../guides/building.md); Linux remains unverified.

Vocabulary:

| Term | Meaning |
| --- | --- |
| supported | implemented, and a fixture proves it |
| approximated | implemented as the closest standard representation, with a diagnostic saying so |
| preserved | the source value is kept on the stage, not interpreted |
| unsupported | read and reported with a diagnostic, not authored |
| unverified | implemented, but no fixture proves it yet |
| — | nothing implemented |

No row says "supported" without a fixture.

## 1. Container

| Capability | Status | Fixture | Intended in |
| --- | --- | --- | --- |
| `.blend` registration (`usd-fileformat:blend`) | supported | `header_only.blend` | Phase 0 |
| legacy header | supported | `header_only.blend`; malformed header fixtures; `blendFile.header` | Phase 0 |
| Blender 5 header | supported | `blendFile.header`; `corpus/blender-5.2.2/Untitled.blend` | Phase 0 |
| legacy block layout | — | | Phase 1 |
| Blender 5 block layout | — | | Phase 1 |
| gzip | — | | Phase 1 |
| Zstandard header probe (bounded; not full-file validation) | supported | `blendFile.header`; `corpus/blender-5.2.2/Untitled.blend` | Phase 0 |
| Zstandard full-container decompression | — | | Phase 1 |
| SDNA | — | | Phase 1 |
| `blend_inspect` | — | | Phase 1 |

## 2. Blender versions

| Version | Status | Fixture directory |
| --- | --- | --- |
| 4.5 LTS | — | |
| 5.x | — | |

## 3. Stage

| Capability | Status | Fixture | Intended in |
| --- | --- | --- | --- |
| `/Asset`, `defaultPrim`, `geo`, `mtl` | supported | `header_only.blend`, `test_stage.py`, golden | Phase 0 |
| Y-up, meters | supported | `header_only.blend`, `test_stage.py`, golden | Phase 0 |
| objects, parenting, transforms | — | | Phase 2 |
| meshes: topology, normals, UVs | — | | Phase 2 |
| deterministic identifiers | — | | Phase 2 |
| materials: Principled BSDF subset | — | | Phase 3 |
| material subsets and binding | — | | Phase 3 |
| external image textures | — | | Phase 3 |
| packed images | — | | — |
| cameras | — | | Phase 4 |
| lights | — | | Phase 4 |
| collections | — | | Phase 4 |
| object animation | — | | Phase 5 |
| armatures, skinning, skeletal animation | — | | Phase 6 |
| shape keys | — | | Phase 6 (investigation) |
| curves, point clouds | — | | — |
| modifiers, Geometry Nodes | — | | Phase 7 (host backend only) |
| `metadataOnly` fast path | — | | Phase 8 |

## 4. Backends

| Backend | Status | Intended in |
| --- | --- | --- |
| native | — | Phase 1 onward |
| Blender host | — | Phase 7 |

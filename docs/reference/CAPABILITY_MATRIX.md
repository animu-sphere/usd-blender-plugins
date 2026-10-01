# Capability matrix

This page is the **only** document that says what is implemented. Every other
document describes intended behavior and links here.

The tables below own capability status, not delivery status; phase status is in
the [roadmap table](../roadmap/README.md#status-at-a-glance).
Synthetic headers and the Blender-written `empty.blend` below live in
`plugins/usdBlendFileFormat/tests/fixtures/`;
the contributor-provided Blender files live in `tests/corpus/` beside them.
Header validation does not establish scene compatibility. Fixture generation
requirements and evidence are in its
[provenance and checks](../../plugins/usdBlendFileFormat/tests/fixtures/README.md#blender-written-empty-scene);
build and test procedures are in [the build guide](../guides/building.md).

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
| `.blend` registration (`usd-fileformat:blend`) | supported | `header_only.blend`; `empty.blend` | Phase 0 |
| legacy header | supported | `header_only.blend`; `corpus/blender-4.5.13/Untitled.blend`; malformed header fixtures; `blendFile.header` | Phase 0 |
| Blender 5 header | supported | `empty.blend`; `blendFile.header`; `corpus/blender-5.2.2/Untitled.blend` | Phase 0 |
| legacy block layout (enumeration only) | supported | `corpus/blender-4.5.13/Untitled.blend`; synthetic 4.5 containers for both pointer widths and byte orders in `blendFile.header` | Phase 1 |
| Blender 5 block layout (enumeration only) | supported | `empty.blend`; `corpus/blender-5.2.2/Untitled.blend`; synthetic format-1 containers and sparse 64-bit fields in `blendFile.header` | Phase 1 |
| real 4.5/5.x block-kind equivalence (enumeration only) | supported | both corpus files enumerate the same 20 block kinds in `blendFile.header`; [comparison evidence](../../plugins/usdBlendFileFormat/tests/corpus/README.md#real-file-comparison) | Phase 1 |
| block framing and caller-supplied count limit | supported | terminal `ENDB`, exact count limits, negative fields, unaligned payloads, truncated boundaries, oversized lengths, trailing bytes, failed reads and unknown codes in `blendFile.header` | Phase 1 |
| gzip header probe (bounded; not full-file validation) | supported | synthetic stored-block members and an independently encoded DEFLATE stream in `blendFile.header` | Phase 1 |
| gzip full-container decompression (bytes only; explicit limits) | supported | generated gzip members around `empty.blend` and both decoded corpus files; stored-block and independent DEFLATE vectors in `blendFile.header`; no Blender-written gzip fixture | Phase 1 |
| Zstandard header probe (bounded; not full-file validation) | supported | `blendFile.header`; `corpus/blender-5.2.2/Untitled.blend` | Phase 0 |
| Zstandard full-container decompression (bytes only; explicit limits) | supported | `corpus/blender-5.2.2/Untitled.blend`; raw frames around `empty.blend` and the 4.5.13 corpus bytes; checksum and RLE vectors in `blendFile.header` | Phase 1 |
| full-file input, output, ratio and window limits | supported | exact byte/ratio boundaries, high-ratio streams, oversized windows and invalid limits in `blendFile.header` | Phase 1 |
| SDNA tables, member layouts and name lookup | supported | all structures in `empty.blend` and both real corpus files; synthetic 32/64-bit, little/big-endian schemas, arrays, function pointers and empty structures in `blendFile.header` | Phase 1 |
| SDNA malformed-input diagnostics | supported | all short payload prefixes, invalid sections/counts/indices/names, duplicate records, size mismatches, overflowing arrays and trailing bytes in `blendFile.header` | Phase 1 |
| `blend_inspect` | — | | Phase 1 |

Full-stream byte reading validates compression and the decoded header, not
blocks, `ENDB` or SDNA. It does not change the importer's header-only path.
Caller-supplied limits and their semantics are defined in the
[blend contract](../design/BLEND_CONTRACT.md#41-full-stream-byte-reading);
production defaults remain the open BLEND-O5 decision in that document.

`ReadBlocks` separately enumerates an uncompressed source, including `ENDB`,
under an explicit block-count limit. Its
[boundary](../design/BLEND_CONTRACT.md#64-block-enumeration-boundary) checks
framing without reading payloads or validating SDNA. The real 4.5.13 corpus
proves the 64-bit little-endian legacy layout; other legacy layouts still
have only synthetic evidence. Block enumeration establishes no scene
compatibility and does not change the importer's header-only path.

`ReadDna` separately decodes one bounded DNA1 payload into an owning schema,
with member offsets and lookup by base name. Its
[boundary](../design/BLEND_CONTRACT.md#71-schema-decoding-boundary) does not
validate other blocks against the schema, reconstruct pointers or read scene
values. Real-file SDNA evidence is limited to 64-bit little-endian files;
the other layouts have synthetic evidence only. Blender-version and scene
compatibility remain unclaimed, and the importer remains header-only.

## 2. Blender versions

| Version | Status | Fixture directory |
| --- | --- | --- |
| 4.5 LTS | — | |
| 5.x | — | |

## 3. Stage

| Capability | Status | Fixture | Intended in |
| --- | --- | --- | --- |
| `/Asset`, `defaultPrim`, `geo`, `mtl` | supported | `header_only.blend`, `empty.blend`, `test_stage.py`, goldens | Phase 0 |
| Y-up, meters | supported | `header_only.blend`, `empty.blend`, `test_stage.py`, goldens | Phase 0 |
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

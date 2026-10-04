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
| block framing and caller-supplied count limit | supported | terminal `ENDB`, exact count limits, negative fields, unaligned payloads, trailing bytes, failed reads and unknown codes in `blendFile.header`; every block boundary/header prefix, payload cut points and oversized lengths in `empty.blend`, both corpus files and all synthetic layouts, with checked source-read ranges | Phase 1 |
| gzip header probe (bounded; not full-file validation) | supported | synthetic stored-block members and an independently encoded DEFLATE stream in `blendFile.header` | Phase 1 |
| gzip full-container decompression (bytes only; explicit limits) | supported | generated gzip members around `empty.blend` and both decoded corpus files; stored-block and independent DEFLATE vectors in `blendFile.header`; no Blender-written gzip fixture | Phase 1 |
| Zstandard header probe (bounded; not full-file validation) | supported | `blendFile.header`; `corpus/blender-5.2.2/Untitled.blend` | Phase 0 |
| Zstandard full-container decompression (bytes only; explicit limits) | supported | `corpus/blender-5.2.2/Untitled.blend`; raw frames around `empty.blend` and the 4.5.13 corpus bytes; checksum and RLE vectors in `blendFile.header` | Phase 1 |
| full-file input, output, ratio and window limits | supported | exact byte/ratio boundaries, high-ratio streams, oversized windows and invalid limits in `blendFile.header`; pinned [real-input minima](../../plugins/usdBlendFileFormat/tests/corpus/README.md#compression-measurements) and immediately smaller budget rejection | Phase 1 |
| SDNA tables, member layouts and name lookup | supported | all structures in `empty.blend` and both real corpus files; synthetic 32/64-bit, little/big-endian schemas, arrays, function pointers and empty structures in `blendFile.header` | Phase 1 |
| SDNA malformed-input diagnostics | supported | all short payload prefixes, invalid sections/counts/indices/names, duplicate records, size mismatches, overflowing arrays and trailing bytes in `blendFile.header` | Phase 1 |
| saved-address pointer map (ID and DATA; exact keys only) | supported | all reference-target addresses in `empty.blend` and both corpus files; synthetic null, unresolved, duplicate, metadata-collision and 64-bit keys in `blendFile.header` | Phase 1 |
| raw ID datablock type/name enumeration | supported | every ID block in `empty.blend` and both corpus files; synthetic 32/64-bit, little/big-endian layouts, embedded ID offsets and raw name bytes in `blendFile.header` | Phase 1 |
| raw ID range, index, count and name diagnostics | supported | out-of-range SDNA indices for every ID in `empty.blend` and both corpus files; synthetic out-of-range payloads/indices, size/count mismatches, invalid embedded members and unterminated names in `blendFile.header` | Phase 1 |
| borrowed SDNA block/member/array views and saved pointer reads | supported | both corpus files' `FileGlobal.curscene` resolve to `Scene` / `SCScene`; Scene-only `empty.blend` preserves null `curscene`; synthetic 32/64-bit, little/big-endian nested members, second block elements, multidimensional arrays and pointer/function-pointer storage in `blendFile.header` | Phase 2 |
| typed SDNA scalar reads and view diagnostics | supported | `Scene.unit.scale_length` in all three real files; synthetic signed minima, unsigned maxima, IEEE float/double, nonfinite values, wrong types/shapes, truncated spans and invalid ranges/counts/indices in `blendFile.header` | Phase 2 |
| `blend_inspect`: summary, `--blocks`, `--dna`, raw `--objects` | supported | `empty.blend` and both real corpus files in `blendInspect.cli`; argument/limit errors, missing/duplicate/malformed DNA1 and UTF-8 path regressions | Phase 1 |

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

`BuildPointerMap` and `ListDatablocks` separately provide
[exact-key resolution](../design/BLEND_CONTRACT.md#81-pointer-map-boundary) and
[raw ID records](../design/BLEND_CONTRACT.md#82-raw-datablock-boundary).
Metadata blocks do not enter the pointer map. Names retain their stored
two-byte prefixes, which can differ from block codes (`SN`/`SR` screens).
These APIs do not read pointer-valued members, linked libraries, lists or scene
graphs; non-ID payloads are not checked against SDNA by them. Real-file evidence remains
64-bit little-endian, and other layouts have synthetic evidence. The importer
continues to read headers only.

`ViewDnaBlock` separately validates one caller-selected block against SDNA,
then exposes borrowed member/array views, saved pointers and typed scalar
values under the [value boundary](../design/BLEND_CONTRACT.md#83-borrowed-sdna-value-boundary).
The backing bytes and schema must remain alive and unmodified. Tests resolve
the saved current Scene in both corpus files and read its embedded ID name;
the Scene-only library fixture has a null `curscene`, not a selected Scene.
All three files' `Scene.unit.scale_length` values are read as source facts,
without unit conversion or an end-to-end physical-equivalence claim. No
Scene-selection policy, library fallback, Collection/list walk, graph validation
or Scene IR publication is provided by `blendFile`; the separate semantic
selection API is listed in section 5. The importer and inspection tool remain
unchanged; real-file layout evidence is still 64-bit little-endian only.

The inspection tool reports raw file-wide ID counts and saved Object names,
not a scene graph. Compressed inputs require explicit limits; unknown block
codes remain recoverable stderr diagnostics. Its
[CLI contract](../design/DESIGN_POLICY.md#54-blend_inspect--the-tool) and
[build commands](../guides/building.md#inspection-tool) define this boundary.

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

## 5. Scene IR

These are library-only capabilities, not `.blend` scene compatibility or
authored-stage support. The importer remains header-only.

| Capability | Status | Fixture | Intended in |
| --- | --- | --- | --- |
| owning object/mesh IR, optional parent and shared-mesh indices | supported | synthetic empty, parented and shared-mesh records and copy-independence checks in `blendScene.ir` | Phase 2 |
| point/direction basis rotation and mesh/empty world-matrix conjugation | supported | synthetic asymmetric matrix, translated/rotated/scaled parent-child composition, direction-length and right-handed winding checks in `blendScene.ir` | Phase 2 |
| validated distance/position and affine mesh/empty translation normalization to meters | supported | synthetic scales `1`, `0.01`, `0.001`, `10`, equivalent cube extents, parent-child composition, unchanged non-translation matrix entries and invalid/overflow rejection in `blendScene.ir` | Phase 2 |
| Blender-written unit-scale equivalence through native decoding and USD authoring | — | | Phase 2 |
| saved active Scene selection and owning source metadata | supported | both normal-save corpus files select `Scene`; Scene-only `empty.blend` fails without fallback; synthetic two-Scene, reordered, 32/64-bit and little/big-endian layouts in `blendScene.ir` | Phase 2 |
| saved-scene missing/invalid/linked-reference and source-metadata diagnostics | supported | missing/duplicate/wrong GLOB, null/absent/interior/wrong-type references, duplicate addresses, linked ID, invalid names/unit scales and SDNA errors with exact fatal block context in `blendScene.ir` | Phase 2 |
| saved Collection membership and owning Object names/indices | supported | both normal-save 4.5.13/5.2.2 corpus files select Camera/Cube/Light; synthetic nested/shared/reordered/empty Collections and Object deduplication across all four layouts in `blendScene.ir` | Phase 2 |
| Collection/ListBase reference, cycle and explicit traversal-budget diagnostics | supported | null/absent/interior/wrong-type/count targets, linked IDs, invalid names, endpoint/backlink/node-sharing errors, list/Collection cycles, exact visit/depth limits and 256-child chains in `blendScene.ir` | Phase 2 |
| saved Object parent references and optional parent block indices | supported | null parents in both normal-save corpus files; synthetic selected/unselected/shared parents and block reordering across all four layouts in `blendScene.ir` | Phase 2 |
| parent-reference, cycle and explicit traversal-budget diagnostics | supported | synthetic absent/interior/wrong-code/type/count references, linked parents, invalid names/SDNA shapes, self/ancestor cycles and 256-parent chains at exact visit/depth limits; every corpus Object has self/interior/absent pointer mutations in `blendScene.ir` | Phase 2 |
| saved Object data ID references and optional data block indices | supported | both normal-save corpus files resolve Camera/Cube/Light to `CA`/Camera, `ME`/Mesh and `LA`/Lamp; 4.5 stores `void *data`, 5.2 stores `ID *data`; synthetic null/shared/parent-only/reordered data across all four layouts in `blendScene.ir` | Phase 2 |
| data ID-reference, linked-target and explicit visit-budget diagnostics | supported | synthetic absent/interior/metadata/DATA/wrong-count targets, malformed member/ID shapes, linked data, invalid names, exact/one-smaller budgets and Object/data visit deduplication; every corpus Object has null/interior/absent data-pointer mutations in `blendScene.ir` | Phase 2 |
| saved Object type, render visibility, transform flags and type-specific data ID requirements | supported | opt-in `SelectSceneObjectValues`; four synthetic layouts cover all mapped kinds, missing/wrong data, scalar shapes, parent-only values and ownership; both 4.5.13/5.2.2 corpus files cover native kinds, saved short/int visibility and visibility/data mutations in `blendScene.ir` | Phase 2 |
| immediate instance Collection references and explicit visit-budget diagnostics | supported | four synthetic layouts cover saved member names, null/absent/interior/wrong/count/linked/name targets, inactive references, shared and already-visited targets, reordered indices and exact budgets; both corpus files cover null references and pointer/flag mutations in `blendScene.ir` | Phase 2 |
| recursive instance graph validation | supported | four synthetic layouts cover nested missing/interior/wrong-type/linked targets, self/ancestor cycles through CollectionObject membership, shared targets, preserved membership and generic selection, and 256-child instance subgraphs with exact/one-smaller visit/depth budgets; both corpus files reject self-instancing master Collections in `blendScene.ir` | Phase 2 |
| native Mesh/Empty Scene IR decoding, render visibility and selected parent indices | supported | `DecodeScene` composes Object-value selection; four synthetic layouts cover mixed Mesh/Empty Scenes, shared Mesh indices, parent-only Objects, block reordering, ownership and explicit failures for unsupported kinds/data/instances; both corpus SDNA layouts decode in-memory Object-kind/data mutations without rewriting fixtures in `blendScene.ir` | Phase 2 |
| native Euler/Quaternion/Axis-Angle local/world transform construction and meter normalization | supported | four synthetic layouts cover active/inactive channels, normalized Quaternion/Axis-Angle, delta order, malformed/nonfinite storage, column-first parent inverse, four unit scales and shared/deep parents in `blendScene.ir`; [Blender-written 4.5.13/5.2.2 transform oracles](../../tests/fixtures/native-transforms/README.md) compare 27 Empty objects per file, all six Euler orders, Quaternion/Axis-Angle, delta channels, nonuniform/negative/zero scale, parent inverses, three-level hierarchy and converted parent/local composition in `blendScene.transforms` | Phase 2 |
| native source-only animation/constraint/modifier and shape-key presence diagnostics | supported | nonnull `adt`, constraint endpoints and Mesh modifiers produce contextual recoverable `BLEND_SCENE_EVALUATION_UNAPPLIED`; Mesh keys produce `BLEND_MESH_EVALUATION_UNAPPLIED`, without following or applying evaluation data in four synthetic layouts in `blendScene.ir` | Phase 2 |
| native Mesh source positions, polygon offsets and corner vertex indices | supported | real 4.5.13 CustomData and 5.2.2 AttributeArray Cube Mesh payloads with pinned points and all face indices; both storage forms across four synthetic layouts cover shared/mixed objects, ownership, repeated/reordered reads and four unit scales in `blendScene.ir` | Phase 2 |
| native flat face-varying corner normals | supported | all 24 outward Cube corner normals in both corpus files; two synthetic triangles in both storage forms across four layouts, with normals unchanged by unit scale in `blendScene.ir` | Phase 2 |
| native named indexed UV maps and saved render map | supported | real `UVMap` in both corpus files; two synthetic maps with exact deduplicated values/indices and non-first render map in both storage forms across four layouts in `blendScene.ir` | Phase 2 |
| native empty/invalid/unsupported Mesh storage diagnostics | supported | empty shared Mesh, negative/excessive counts, missing/interior/non-DATA pointers, wrong array lengths/counts/SDNA, malformed names, invalid offsets/vertex indices, nonfinite values, degenerate/smooth/custom normals, flagged/constant/unsupported storage and invalid UV selectors in `blendScene.ir` | Phase 2 |
| native smooth or custom split normals, constant attribute storage and legacy fixed Mesh/MLoopUV storage | unsupported | explicit missing-core, smooth/custom-normal, constant/flagged and legacy-UV failures in `blendScene.ir`; no Blender-written smooth/custom-normal fixture | Phase 2 |
| parent-relative transforms | — | | Phase 2 |

The [IR contract](../design/DESIGN_POLICY.md#521-scene-ir-foundation)
defines row-major, column-vector matrix semantics and meter-space distances.
Basis helpers do not scale distances; `UnitConversion` combines source-distance
normalization with the basis rotation for positions and mesh/empty matrices.
Source unit metadata is provenance only. STAGE-O1 remains open for
Blender-written source-field and end-to-end evidence under the selected
[unit policy](../design/STAGE_CONTRACT.md#61-scene-units); NAME-O1 remains open
in its owning design document. No helper performs native decoding or USD
authoring.

The separate [saved-scene selection boundary](../design/DESIGN_POLICY.md#522-saved-scene-selection-boundary)
copies prefix-stripped raw Scene names, header versions and positive finite
source unit scales. It selects through `FileGlobal.curscene`, never a first-Scene
fallback, and rejects linked active scenes without external-file access. It
does not populate objects/meshes, traverse collections or establish native-to-USD
unit equivalence. Its `blockIndex` refers to the caller's same ordered records.
Real-file selection evidence remains 64-bit little-endian; other layouts have
synthetic evidence only. The importer and inspection tool remain unchanged.

The separate [Collection membership boundary](../design/DESIGN_POLICY.md#523-saved-collection-membership-boundary)
selects saved Object membership from that Scene's master Collection, with exact
references, bounded iterative traversal and fatal errors instead of partial
results. It retains each reachable Object once in saved discovery order,
including Camera and Light records without decoding their values. It returns
owning raw names and caller-sequence indices, not object transforms, meshes,
visibility, evaluated/view-layer state, Collection instances or a populated IR.
Data references are separately validated below; instance references are not
followed or validated. Collection/Object linked
IDs fail without external-file access. Real-file membership evidence is
64-bit little-endian only; other
layouts, nested/shared graphs and malformed cases have synthetic evidence.

The [parent-reference boundary](../design/DESIGN_POLICY.md#524-saved-object-parent-reference-boundary)
validates selected Objects' saved parent chains with the same explicit budgets.
Parent-only Objects count once but do not join membership. Optional parent
indices belong to the caller's blocks, not the selected Object vector or IR.
Completed chains are shared; depth limits bound active unfinished expansion,
not the longest complete path. Objects outside membership, its ancestors and
immediate data targets remain unvalidated. Real-file evidence covers null parents and
mutated invalid pointers only; nontrivial parenting is synthetic. No transform,
parenting-mode or Blender-oracle equivalence is established.

The [data-reference boundary](../design/DESIGN_POLICY.md#525-saved-object-data-reference-boundary)
validates the immediate generic ID target for selected and parent-only Objects,
retaining an optional caller-block index. Null is allowed; shared data consumes
one visit, including deduplication against reached Objects. Linked IDs fail
without external access. It does not enforce `Object.type`-specific data
requirements or follow data-internal references, so neither a null mesh data
pointer nor an otherwise valid local ID of the wrong semantic type is rejected.
No Mesh values, data-cycle policy, instance graph or populated IR is added.
Real data-edge evidence remains 64-bit little-endian; sharing, parent-only
targets, budgets and other layouts have synthetic evidence.

The [native decoding boundary](../design/DESIGN_POLICY.md#527-native-scene-decoding-boundary)
publishes owning Scene IR only after saved Object-value and recursive graph
validation succeeds. It constructs source Euler, Quaternion and Axis-Angle
transforms, including applicable delta channels and ordinary Object parent inverses, then normalizes world
translations and the basis once. Mesh and data-less Empty objects with zero
transform flags are accepted; other kinds, Image Empty data, active instances,
unknown rotation modes and non-ordinary parenting modes fail explicitly, with no partial IR.
Parent-only Objects affect world space but do not join membership. Immediate
selected parents become IR indices; other parents leave an IR root with its
complete world matrix. Source names and render visibility are retained,
identifiers remain empty, and selected Mesh data is decoded once per shared
target under the [Mesh storage boundary](../design/DESIGN_POLICY.md#528-native-mesh-storage-boundary).
Animation/constraint/modifier and shape-key presence is reported without
evaluation. The Cube's Mesh bytes in both corpus files are unchanged; non-Mesh
Objects are mutated to Empty in memory to isolate the supported scene scope.
Those files remain unsupported as complete Scenes because Camera/Light decoding
is not introduced. Separate Blender-written Empty fixtures compare native
world matrices and parent/local composition with saved transform oracles.
The native matrix tolerance is `2e-5 * (1 + abs(expected))` per component;
the decoder keeps strict finite/affine validation. No backend,
USD authoring, native-to-USD unit equivalence or importer connection is claimed.

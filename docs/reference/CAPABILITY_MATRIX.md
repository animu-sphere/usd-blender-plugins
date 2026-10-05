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
| regression-only | existing fixture-backed behavior retained outside the maintained Blender-version support scope |
| — | nothing implemented |

No row says "supported" without a fixture.

## 1. Container

| Capability | Status | Fixture | Intended in |
| --- | --- | --- | --- |
| `.blend` registration (`usd-fileformat:blend`) | supported | `single_cube.blend`; registered-plugin `test_stage.py` | Phase 0 |
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
blocks, `ENDB` or SDNA. The importer composes those separate boundaries for
uncompressed and decoded gzip/Zstandard inputs.
Caller-supplied limits and their semantics are defined in the
[blend contract](../design/BLEND_CONTRACT.md#41-full-stream-byte-reading);
the accepted [standard full-stream policy](../design/BLEND_CONTRACT.md#42-standard-full-stream-limit-policy)
is opt-in, not an implicit API or CLI fallback. Larger generated-input
measurements are recorded in the
[dated report](../reports/2026-10-05-compression-policy.md); those files are
not committed fixtures and add no scene-compatibility claim.

`ReadBlocks` separately enumerates an uncompressed source, including `ENDB`,
under an explicit block-count limit. Its
[boundary](../design/BLEND_CONTRACT.md#64-block-enumeration-boundary) checks
framing without reading payloads or validating SDNA. The real 4.5.13 corpus
proves the 64-bit little-endian legacy layout; other legacy layouts still
have only synthetic evidence. Block enumeration establishes no scene
compatibility by itself.

`ReadDna` separately decodes one bounded DNA1 payload into an owning schema,
with member offsets and lookup by base name. Its
[boundary](../design/BLEND_CONTRACT.md#71-schema-decoding-boundary) does not
validate other blocks against the schema, reconstruct pointers or read scene
values. Real-file SDNA evidence is limited to 64-bit little-endian files;
the other layouts have synthetic evidence only. Blender-version and scene
compatibility beyond the fixture-backed Mesh/Empty scope remains unclaimed.

`BuildPointerMap` and `ListDatablocks` separately provide
[exact-key resolution](../design/BLEND_CONTRACT.md#81-pointer-map-boundary) and
[raw ID records](../design/BLEND_CONTRACT.md#82-raw-datablock-boundary).
Metadata blocks do not enter the pointer map. Names retain their stored
two-byte prefixes, which can differ from block codes (`SN`/`SR` screens).
These APIs do not read pointer-valued members, linked libraries, lists or scene
graphs; non-ID payloads are not checked against SDNA by them. Real-file evidence remains
64-bit little-endian, and other layouts have synthetic evidence. The importer
composes native decoding separately from these syntax-only APIs.

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

The [version policy](../design/BLEND_CONTRACT.md#9-version-support) limits
the current support guarantee to Blender 5.x and defers full older-version
compatibility to Phase 8. Existing older-version decoders, fixtures and tests
are retained; this does not introduce runtime version rejection. Capability
rows elsewhere on this page record implemented, tested behavior, including
older layouts, rather than expanding the maintained Blender-version range.
Even within 5.x, only the fixture-backed storage and features below are claimed,
not every 5.x release or scene.

| Version | Support status | Fixture evidence |
| --- | --- | --- |
| 5.2.2 | supported for tested Mesh/Empty storage and diagnostic-bearing unsupported-data Xforms; not release-wide scene compatibility | `single_cube.blend`; `native-scene`, `native-transforms`, `native-units`, `native-mesh`, `native-normals`: 5.2.2; the compressed corpus and generated encodings prove the bounded importer path; `native-scene/fallbacks.blend` proves Camera/Light/text/Image Empty hierarchy only |
| other 5.x releases | unverified as Scene/Mesh compatibility targets; the 5.x design target is not a blanket support claim | no Blender-written release-specific Scene/Mesh oracle evidence |
| 4.5 LTS | regression-only; no current version support guarantee | `native-scene`, `native-transforms`, `native-units`: 4.5.13 |
| 3.3 | regression-only; no current version support guarantee | `native-mesh` and `native-normals`: 3.3.21 legacy Mesh storage and default/auto-smooth normals; no other 3.x–4.4 version claim |

The finite [Phase 2 exit criteria](../design/DESIGN_POLICY.md#142-phase-2-exit-criteria)
use the 5.2.2 fixture-proven scope. Completing that milestone does not upgrade
the unverified releases above; later compatibility claims need their own
bounded evidence. Existing synthetic byte-order/pointer-width and storage
tests are retained without being promoted to Blender-written release proof.

## 3. Stage

| Capability | Status | Fixture | Intended in |
| --- | --- | --- | --- |
| `/Asset`, `defaultPrim`, `geo`, `mtl` | supported | `single_cube.blend`, integrated Scene fixtures, `test_stage.py`, cube golden | Phase 0 |
| Y-up, meters | supported | cube, integrated Scene and eight multi-scale fixtures in `test_stage.py` | Phase 0 |
| Mesh/Empty objects, parenting, transforms and own render visibility | supported | both integrated Scene oracles in `test_stage.py`; shared Meshes, parent-only Object, nonuniform/negative scales and sheared parent inverses | Phase 2 |
| unsupported Camera/Light/text/Image Empty data as hierarchy-preserving Xforms | approximated | [5.2.2 fallback oracle](../../tests/fixtures/native-scene/README.md#unsupported-data-fallback-oracle), `test_stage.py`: supported Mesh children, shared unsupported Camera data, parent-only Camera, visibility, source matrices, metadata/repeats/references and five contextual warnings; no Camera/Light/image/text data is authored | Phase 2 |
| meshes: polygon topology, normals, indexed UVs and extent | supported | `single_cube.blend` and golden; both integrated Scene oracles in `test_stage.py`; native-to-authoring checks in both CMake modes | Phase 2 |
| direct Cube display in usdview (Windows, Storm) | supported | `single_cube.blend`; local `test_usdview.py` checks the loaded `.blend`, converged viewport, Cube silhouette and center Mesh pick; [dated evidence](../../plugins/usdBlendFileFormat/tests/fixtures/README.md#cube-milestone-verification); no Linux viewport claim | Phase 2 |
| deterministic identifiers and repeated reads | supported | reserved `mesh_1` child, repeated anonymous layers and referenced geometry in `test_stage.py`; ASCII multi-scale fixtures in `usdBlend.units` | Phase 2 |
| `metadataOnly` hierarchy without geometry attributes | supported | cube and integrated Scenes retain prim types, provenance, transforms and visibility in `test_stage.py`; both authoring CTests check the byte-to-Scene composition | Phase 2 |
| gzip/Zstandard scene importing under bounded full-stream limits | supported | Blender-written 5.2.2 compressed corpus and generated gzip/raw-Zstandard Cube, integrated Scene and fallback encodings in `test_stage.py`; concatenated header splits, decoded-size block bounds, full/metadata equivalence, repeats/references, corruption and contextual decoded errors; exact caller limit boundaries and warning equivalence in `usdBlend.importer`; no Blender-written gzip evidence | Phase 2 |
| materials: Principled BSDF subset | — | | Phase 3 |
| material subsets and binding | — | | Phase 3 |
| external image textures | — | | Phase 3 |
| packed images | — | | — |
| camera schema and data | — | | Phase 4 |
| light schema and data | — | | Phase 4 |
| collections | — | | Phase 4 |
| object animation | — | | Phase 5 |
| armatures, skinning, skeletal animation | — | | Phase 6 |
| shape keys | — | | Phase 6 (investigation) |
| curves, point clouds | — | | — |
| modifiers, Geometry Nodes | — | | Phase 7 (host backend only) |
| `metadataOnly` fast path | — | | Phase 8 |

The registered importer reads complete uncompressed, gzip and Zstandard
containers through native decoding and authoring under the
[importer contract](../design/DESIGN_POLICY.md#532-scene-importer-boundary).
Compressed byte reading explicitly selects the accepted 256 MiB stored,
512 MiB decoded, ratio 4,096 and window-log-23 policy; internal callers can
override all four limits. Uncompressed byte bounds remain input-derived.
Block budgets use decoded size and graph budgets use the enumerated block
count. Compression/limit failures are fatal, without retry or partial layers.
Header-only fixtures fail with `BLEND_BLOCK_MISSING_ENDB`, and the Scene-only
library fails with `BLEND_SCENE_ACTIVE_MISSING`, without scaffold/first-Scene
fallbacks. Tests also cover missing/duplicate/malformed DNA1, truncated/trailing
containers, gzip/Zstandard corruption, exact compression-budget failures,
contextual errors and recoverable block warnings. Both Camera/Cube/Light
corpus files retain Camera/Light as diagnostic-bearing Empty Xforms.
Block/SDNA/Scene offsets are decoded-file positions; compression errors retain
stored-stream context.
`metadataOnly` changes authored output, not decoding cost.

### 3.1 Scene IR USD authoring

These capabilities are the bundle's separately tested `AuthorScene` boundary.
The Stage rows above record its registered-plugin connection; the boundary
itself does not choose compression or fixed production resource budgets.

| Capability | Status | Fixture | Intended in |
| --- | --- | --- | --- |
| Mesh/Empty Xforms, parenting, local matrices and render visibility | supported | asymmetric synthetic affine/shear/nonuniform/negative-scale hierarchy; unchanged Blender-written 4.5.13/5.2.2 transform fixtures compare all 27 Object worlds and locals per version to their oracle within `2e-5` in `usdBlend.authoring` | Phase 2 |
| polygon Meshes, extent, face-varying normals and indexed UVs | supported | synthetic asymmetric points, two triangles, corner normals and four indexed maps pin exact schema types, values and bounds; unchanged independent two-Mesh 4.5.13/5.2.2 fixtures decode and author exact IR arrays in `usdBlend.authoring` | Phase 2 |
| one normalization and duplicated shared Meshes | supported | synthetic source-scale provenance `0.001` leaves already-normalized points/translations untouched; independently editable duplicated Mesh attributes, no USD instancing, explicit empty topology/extent and one shared empty-Mesh warning in `usdBlend.authoring` | Phase 2 |
| Blender-written multi-scale physical equivalence and ASCII naming | supported | eight [4.5.13/5.2.2 unit fixtures](../../tests/fixtures/native-units/README.md) decode and author scales `1`, `0.01`, `0.001`, `10` with matching one-meter cubes, translated/reflected/sheared parenting, normals, indexed UVs, extent, Y-up and `metersPerUnit = 1`; explicit punctuation/natural-suffix/UTF-8 fallback identifiers, display/provenance and fixed-child/render-UV reservations in `usdBlend.units` | Phase 2 |
| deterministic names, ordering and provenance | supported | fixed `st` reservation, UV source-name collisions, malformed UTF-8 display repair, Object/UV permutations, repeated native reads and reversed native blocks produce identical layer text; root metadata and geometry survive references in `usdBlend.authoring`; fallback/no-active-map UV cases in `blendScene.naming` | Phase 2 |
| no-partial-layer input diagnostics | supported | invalid graph indices/cycles/identifiers, projective/nonfinite/singular-parent transforms, topology/normal/UV shape/index errors, multiple render maps and exact/one-above float maximum checks in `usdBlend.authoring`; singular roots without children and zero-scale leaves succeed | Phase 2 |

## 4. Backends

| Backend | Status | Intended in |
| --- | --- | --- |
| native, uncompressed and decoded gzip/Zstandard Mesh/Empty input composition | supported; `IBlendBackend` abstraction not introduced | Phase 1 onward |
| Blender host | — | Phase 7 |

## 5. Scene IR

These are native library capabilities, not `.blend` scene compatibility.
The importer composes these native capabilities from decoded bytes; the separate
[Scene IR USD authoring boundary](#31-scene-ir-usd-authoring) now consumes
normalized IR without changing the native libraries' dependency gates.

| Capability | Status | Fixture | Intended in |
| --- | --- | --- | --- |
| owning object/mesh IR, optional parent and shared-mesh indices | supported | synthetic empty, parented and shared-mesh records and copy-independence checks in `blendScene.ir` | Phase 2 |
| point/direction basis rotation and mesh/empty world-matrix conjugation | supported | synthetic asymmetric matrix, translated/rotated/scaled parent-child composition, direction-length and right-handed winding checks in `blendScene.ir` | Phase 2 |
| validated distance/position and affine mesh/empty translation normalization to meters | supported | synthetic scales `1`, `0.01`, `0.001`, `10`, equivalent cube extents, parent-child composition, unchanged non-translation matrix entries and invalid/overflow rejection in `blendScene.ir` | Phase 2 |
| Blender-written unit-scale equivalence through native decoding and USD authoring | supported | eight [4.5.13/5.2.2 unit fixtures](../../tests/fixtures/native-units/README.md) in `usdBlend.units`; independent source-to-meter oracles, cross-scale/version world/local matrices and vertices, strict one-meter points/extents/dimensions, active versus unselected Scene scales and NONE/METRIC/IMPERIAL presentation labels | Phase 2 |
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
| recursive instance graph validation | supported | four synthetic layouts cover nested missing/interior/wrong-type/linked targets, self/ancestor cycles through CollectionObject membership, shared targets, preserved membership and generic selection, and 256-child instance subgraphs with exact/one-smaller visit/depth budgets; both corpus files reject self-instancing master Collections in `blendScene.ir`; [5.2.2 saved graph oracle](../../tests/fixtures/native-scene/README.md#recursive-collection-instance-graph-oracle) in `blendScene.instances` pins shared/multi-level targets, parent-only references, 23 visits/depth 3 and contextual binary mutations; `usdBlend.instances` checks the same cases through registered full/metadata plain/gzip/Zstandard reads in both build modes | Phase 2 |
| native Mesh/Empty Scene IR decoding, render visibility and selected parent indices | supported | `DecodeScene` composes Object-value selection; four synthetic layouts cover mixed Mesh/Empty Scenes, shared Mesh indices, parent-only Objects, block reordering, ownership and explicit failures for unmapped kinds and active instances; both unchanged Camera/Cube/Light corpus inputs decode, with Camera/Light data warnings, in `blendScene.ir` | Phase 2 |
| native known unsupported data as Empty IR Objects | approximated | all mapped non-Mesh kinds and Image Empty across four synthetic layouts in `blendScene.ir`; exact recoverable Object context and unchanged fatal linked/reference/transform policy; [Blender 5.2.2 mixed-kind oracle](../../tests/fixtures/native-scene/README.md#unsupported-data-fallback-oracle) in `blendScene.objectFallbacks` compares 11 selected Objects, two Meshes, unsupported parents and parent-only/shared Camera data under owning/repeated/reversed reads; other mapped kinds have synthetic evidence only | Phase 2 |
| integrated Blender-written Mesh/Empty native Scene oracle | supported | [unchanged 4.5.13/5.2.2 Scene fixtures](../../tests/fixtures/native-scene/README.md) in `blendScene.sceneFixture` combine seven selected Objects, two unique Meshes, three shared-Mesh users, four-level mixed parenting, a parent-only Mesh in an unselected Scene, nested/shared Collection membership, own render bits, fixed `mesh` child reservation, world/local matrices, points/topology/normals and two indexed UV maps; repeated and reversed-block reads preserve every owning IR value | Phase 2 |
| native deterministic Mesh/Empty Object identifiers | supported | `blendScene.naming` covers ASCII examples, literal underscores, unsigned source-byte ordering, natural suffix collisions, parent-scoped Mesh child reservations, repeated reads and all 40,320 permutations of an eight-Object IR with remapped parents; both Mesh storage forms across four synthetic layouts prove native fixed-child reservation and unchanged shared geometry in `blendScene.ir`; unchanged Blender-written 4.5.13/5.2.2 transform fixtures preserve identifiers under reversed blocks in `blendScene.transforms` | Phase 2 |
| UTF-8 display repair and naming diagnostics | supported | valid scalar boundaries, maximal ill-formed subparts, overlong/surrogate/out-of-range/truncated encodings, duplicate sibling names and invalid immediate IR references in `blendScene.naming`; four synthetic layouts retain raw Object names and exact recoverable/fatal block context, including equal malformed names in separate scopes, in `blendScene.ir` | Phase 2 |
| native Euler/Quaternion/Axis-Angle local/world transform construction and meter normalization | supported | four synthetic layouts cover active/inactive channels, normalized Quaternion/Axis-Angle, delta order, malformed/nonfinite storage, column-first parent inverse, four unit scales and shared/deep parents in `blendScene.ir`; [Blender-written 4.5.13/5.2.2 transform oracles](../../tests/fixtures/native-transforms/README.md) compare 27 Empty objects per file, all six Euler orders, Quaternion/Axis-Angle, delta channels, nonuniform/negative/zero scale, parent inverses, three-level hierarchy and converted parent/local composition in `blendScene.transforms` | Phase 2 |
| native source-only animation/constraint/modifier and shape-key presence diagnostics | supported | nonnull `adt`, constraint endpoints and Mesh modifiers produce recoverable `BLEND_SCENE_EVALUATION_UNAPPLIED`; Mesh keys produce `BLEND_MESH_EVALUATION_UNAPPLIED`, without following or applying evaluation data in four synthetic layouts in `blendScene.ir`; [Blender-written 5.2.2 source-only oracle](../../tests/fixtures/native-scene/README.md#source-only-evaluation-oracle) in `blendScene.sourceEvaluation` compares source matrices and original Mesh geometry despite active constraints, subdivision and nonzero shape keys; four Objects (including a parent-only Mesh) and one shared Mesh warn once with exact names/byte/block context; native-to-USD and registered-plugin tests retain source data, metadata/repeats/references and no time samples in both build modes | Phase 2 |
| native Mesh source positions, polygon offsets and corner vertex indices | supported | real 4.5.13 CustomData and 5.2.2 AttributeArray Cube Mesh payloads with pinned points and all face indices; both storage forms across four synthetic layouts cover shared/mixed objects, ownership, repeated/reordered reads and four unit scales in `blendScene.ir` | Phase 2 |
| native flat face-varying corner normals | supported | all 24 outward Cube corner normals in both corpus files; two synthetic triangles in both storage forms across four layouts, with normals unchanged by unit scale in `blendScene.ir` | Phase 2 |
| native smooth point normals and sharp-edge/flat-face split corner fans | supported | [Blender-written 4.5.13/5.2.2 normal oracles](../../tests/fixtures/native-normals/README.md) cover 15 geometry cases grouped into three single-Mesh files per version, including unequal corner angles, closed/open fans, mixed flat/smooth faces, sharp edges, concave polygons and disconnected/nonmanifold/same-direction topology in both normal domains; all 137 corners per version compare within `2e-5` in `blendScene.normals`; four synthetic layouts cover both storage forms, angle weights, missing sharp-face defaults, scale independence and invalid edge/cancellation diagnostics in `blendScene.ir` | Phase 2 |
| modern source corner-angle weights on polygon fans | supported | two [Blender-written 5.2.2 polygon fixtures](../../tests/fixtures/native-normals/README.md#blender-52-polygon-fans-and-corner-angles) compare 495 corners from concave/nonplanar polygons, unequal-area triangles, disconnected fans and 35 angle-sweep wedges in point and split-fan domains; `blendScene.polygonNormals` pins the existing `2e-5` threshold with measured maximum error below `1.6e-6`, unit-length normals and reversed-block determinism; registered-plugin oracles, metadata/repeat/reference checks and both build modes' `usdBlend.authoring` cover composition; four-layout tests preserve 3.3 mathematical weights | Phase 2 |
| native named indexed UV maps and saved render map | supported | real `UVMap` in both corpus files; two synthetic maps with exact deduplicated values/indices and non-first render map in both storage forms across four layouts in `blendScene.ir` | Phase 2 |
| Blender-written empty/loose/UV-free Meshes and UV corner seams | supported | [4.5.13/5.2.2 Mesh-domain fixtures](../../tests/fixtures/native-mesh/README.md) in `blendScene.meshFixture` cover four unique Meshes and a transformed shared user, retained loose points, empty topology/normals/UV arrays, contextual empty warnings, exact first-occurrence UV values/indices including signed zeros and out-of-range coordinates, constant UV coordinates and editing/render-map distinction; repeated/reversed owning reads and registered-plugin stage/extent/metadata/reference checks | Phase 2 |
| native zero-domain dense AttributeArrays with unused nonzero data keys | supported | unchanged 5.2.2 Mesh-domain fixture has empty corner arrays whose nonzero data keys have no DATA block; four synthetic layouts in `blendScene.ir` cover zero-domain position/UV keys and strict logical-size/single-flag validation while nonempty and constant references remain fatal | Phase 2 |
| native empty/invalid/unsupported Mesh storage diagnostics | supported | empty shared Mesh, negative/excessive counts, missing/interior/non-DATA pointers, wrong array lengths/counts/SDNA, malformed names, invalid offsets/vertex/edge indices and inconsistent shared-edge endpoints, nonfinite values, degenerate/cancelling/custom normals, missing split-edge storage, invalid constant storage, flagged/unsupported storage and invalid UV selectors in `blendScene.ir` | Phase 2 |
| native packed custom split normals | supported | four Blender-written custom fixtures per version cover 794 corners, signed-short minima, automatic values, shared/open/closed/mixed fans, integer pair averaging and 199 angle-sweep triangles; all normals compare within `2e-5`, with maximum measured component error below `3e-7` after sharing modern corner-angle weights, in `blendScene.normals`; exact original saved-short/RNA comparison in `blendScene.meshBoundaries`; structured/raw pairs across four synthetic layouts with unit-independence and malformed-length checks in `blendScene.ir` | Phase 2 |
| packed custom normals on modern polygon fans | supported | two [Blender-written 5.2.2 custom polygon fixtures](../../tests/fixtures/native-normals/README.md#blender-52-custom-polygon-fans) add 495 corners on concave/nonplanar polygons, sharp/flat boundaries and angle-sweep wedges; `blendScene.meshBoundaries` compares every saved short pair and dense corner storage to RNA; `blendScene.polygonNormals` pins `2e-5` component tolerance with maximum error `1.11984e-6`, unit-length normals and reversed-block equality; root/standalone `usdBlend.authoring` and registered-plugin oracle/metadata/repeat/reference checks cover composition without decoder changes | Phase 2 |
| native independently constructed multiple Meshes with unique saved addresses | supported | unchanged 4.5.13 `multi.blend` compares both Meshes' different points, topology and normals with a saved oracle, including reversed reads, in `blendScene.normals` | Phase 2 |
| native 5.x Mesh-owned repeated Attribute/AttributeArray saved addresses | supported | unchanged 5.2.2 `multi.blend` decodes both independent Meshes against point/topology/normal oracles under reversed enumeration in `blendScene.normals`; `blendScene.meshBoundaries` preserves strict global-map rejection and tests scoped decoding plus wrong/non-Mesh owners, unreferenced targets, other SDNA types, duplicate IDs and legacy-header rejection | Phase 2 |
| native constant AttributeArray and AttributeSingle storage | supported | `AttributeArray.is_single == 1` and `Attribute.storage_type == 1` across four synthetic layouts, raw/structured positions, integers, face/edge booleans, indexed UVs and packed normals, empty domains, unit independence, repeated/reordered reads and exact invalid-storage context in `blendScene.ir`; Blender-written 5.2.2 `constant.blend` has two constant sharp-face Meshes with a saved normal oracle and a 4.5.13 control in `blendScene.normals`; exact one-byte AttributeSingle shape and in-memory scoped Single-collision/rejection mutations in `blendScene.meshBoundaries`; no Blender-written single-flag AttributeArray fixture | Phase 2 |
| native legacy MLoopUV UV layers | supported | type-16 corner CustomData across four synthetic layouts in `blendScene.ir`; two maps, mixed float2/MLoopUV maps, SDNA member offsets, ignored record flags, exact indexed coordinates including signed zeros and out-of-range UVs, render-name/index selection, unit independence, owning/shared/repeated/reordered reads and empty domains; invalid types/shapes/counts/lengths/references, nonfinite coordinates, duplicate names, flagged/wrong-domain layers and invalid render selectors retain exact fatal context; unchanged 3.3.21 maps compare to their saved oracle in `blendScene.meshFixture` and registered-plugin tests | Phase 2 |
| legacy fixed Mesh position/topology storage | supported | `MVert.co`, signed/unsigned `MLoop.v/e` and contiguous `MPoly` ranges across four synthetic layouts in `blendScene.ir`; shared owning geometry, flat/smooth/mixed and sharp-edge normals, packed automatic normals, float2/MLoopUV maps, four unit scales, loose points, repeated/reversed reads and exact fatal reference/record/shape/value/topology context; invalid or partial modern core storage never falls back; unchanged Blender-written 3.3.21 arrays also decode into IR below | Phase 2 |
| Blender-written 3.3 legacy Mesh decoding and default normals | supported | unchanged [3.3.21 fixture](../../tests/fixtures/native-mesh/README.md#legacy-scene-decoding) in `blendScene.meshFixture` compares full owning IR with its saved Scene oracle: transforms, points, topology, default point/flat normals, exact indexed UVs/zero signs/render selection, empty warnings, membership and sharing under repeated/reversed reads; `usdBlend.authoring` and registered-plugin tests cover stage geometry, extent, metadata and references; `blendScene.legacyMeshStorage` retains raw comparisons plus contextual signed-index, unverified-version and unsupported-normal-mode failures; four synthetic layouts cover absent modern members, CustomData fixed-array aliases, duplicates/mismatches, signed indices and default legacy normals; default Mesh flags `0xd100` and synthetic zero retain point-normal behavior; packed custom normals remain unsupported | Phase 2 |
| Blender-written 3.3 auto-smooth split normals | supported | four [3.3.21 fixtures](../../tests/fixtures/native-normals/README.md#legacy-auto-smooth), 644 corners at 0/60/90/180 degrees, compare saved `0xd120` flags, exact `smoothresh`, owning points/topology and connected-fan corner normals in `blendScene.legacyNormals`; maximum normal component error `8.24e-8`, tolerance `2e-5`; sharp, mixed, boundary, same-direction, nonmanifold, disconnected, open/closed and right-angle threshold cases; `usdBlend.authoring` and registered-plugin oracle/metadata/repeat/reference checks; four synthetic layouts in `blendScene.ir` pin synthetic `0x20`, sharp splitting at pi, zero-angle separation, invalid/nonfinite/out-of-range/wrong-type angle diagnostics with exact fatal context and inactive-angle noninterpretation | Phase 2 |
| parent-relative affine matrix construction from normalized IR worlds | supported | `ParentRelativeTransform`; synthetic roots, shear, pivot swaps, nonuniform/negative/zero scales, four unit scales, extreme finite scales and invalid/singular/overflow rejection in `blendScene.ir`; Blender-written 4.5.13/5.2.2 local matrices and reconstructed worlds, including reversed-read equality, in `blendScene.transforms` | Phase 2 |

The [IR contract](../design/DESIGN_POLICY.md#521-scene-ir-foundation)
defines row-major, column-vector matrix semantics and meter-space distances.
Basis helpers do not scale distances; `UnitConversion` combines source-distance
normalization with the basis rotation for positions and mesh/empty matrices.
`ParentRelativeTransform` constructs locals from normalized IR worlds without
conversion, requiring an invertible authored parent but not an invertible
child or root. Row-scaled double-precision rank checks reject singular or
numerically singular parents; invalid inputs and arithmetic overflow also
fail explicitly. It does not change Scene decoding or store local matrices.
Source unit metadata is provenance only. Separate Blender-written multi-scale
native-to-USD evidence freezes the
[unit policy](../design/STAGE_CONTRACT.md#61-scene-units) and
[ASCII identifier policy](../design/NAMING_POLICY.md#71-resolved-decisions).
No IR helper performs native decoding or USD authoring.

The separate [saved-scene selection boundary](../design/DESIGN_POLICY.md#522-saved-scene-selection-boundary)
copies prefix-stripped raw Scene names, header versions and positive finite
source unit scales. It selects through `FileGlobal.curscene`, never a first-Scene
fallback, and rejects linked active scenes without external-file access. It
does not populate objects/meshes, traverse collections or establish native-to-USD
unit equivalence. Its `blockIndex` refers to the caller's same ordered records.
Real-file selection evidence remains 64-bit little-endian; other layouts have
synthetic evidence only. The importer composes this selection; the inspection
tool remains syntax-only.

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
The integrated Scene fixtures additionally exercise nested Collections and
an Object shared by master and nested Collection membership.

The [parent-reference boundary](../design/DESIGN_POLICY.md#524-saved-object-parent-reference-boundary)
validates selected Objects' saved parent chains with the same explicit budgets.
Parent-only Objects count once but do not join membership. Optional parent
indices belong to the caller's blocks, not the selected Object vector or IR.
Completed chains are shared; depth limits bound active unfinished expansion,
not the longest complete path. Objects outside membership, its ancestors and
immediate data targets remain unvalidated. Corpus mutations cover invalid
pointers; the integrated Scene fixtures additionally prove selected and
parent-only Mesh/Empty parent references. Transform/oracle comparisons belong
to the composed native decoding boundary, not generic reference selection.

The [data-reference boundary](../design/DESIGN_POLICY.md#525-saved-object-data-reference-boundary)
validates the immediate generic ID target for selected and parent-only Objects,
retaining an optional caller-block index. Null is allowed; shared data consumes
one visit, including deduplication against reached Objects. Linked IDs fail
without external access. It does not enforce `Object.type`-specific data
requirements or follow data-internal references, so neither a null mesh data
pointer nor an otherwise valid local ID of the wrong semantic type is rejected.
No Mesh values, data-cycle policy, instance graph or populated IR is added.
Real data-edge evidence remains 64-bit little-endian; the integrated Scene
fixtures prove shared and parent-only Mesh data targets. Budgets and other
layouts retain synthetic evidence.

The [native decoding boundary](../design/DESIGN_POLICY.md#527-native-scene-decoding-boundary)
publishes owning Scene IR only after saved Object-value and recursive graph
validation succeeds. It constructs source Euler, Quaternion and Axis-Angle
transforms, including applicable delta channels and ordinary Object parent inverses, then normalizes world
translations and the basis once. Mesh and data-less Empty objects with zero
transform flags or cached negative-world-handedness bit 2 are accepted;
signed channels and parent composition reconstruct reflections without
applying the bit again. The unit fixtures prove saved bit 2 in both versions;
four-layout synthetic cases keep source channels authoritative and reject
all other non-instance bits, alone or combined with bit 2.
Known non-Mesh kinds and Image Empty data preserve Empty IR Objects with
recoverable `BLEND_SCENE_OBJECT_DATA_UNSUPPORTED`; their source transforms,
hierarchy and render visibility remain intact without invented geometry.
Unmapped kinds, active instances, unknown rotation modes and non-ordinary
parenting modes fail explicitly, with no partial IR.
Parent-only Objects affect world space but do not join membership. Immediate
selected parents become IR indices; other parents leave an IR root with its
complete world matrix. Source names and render visibility are retained,
the [Object naming pass](../design/NAMING_POLICY.md#41-native-object-naming-boundary)
assigns deterministic ASCII identifiers without changing raw names or discovery
order, and selected Mesh data is decoded once per shared
target under the [Mesh storage boundary](../design/DESIGN_POLICY.md#528-native-mesh-storage-boundary).
Animation/constraint/modifier and shape-key presence is reported without
evaluation. Both unchanged corpus files now decode their Cube Mesh and retain
Camera/Light as diagnostic-bearing Empty Objects. Existing in-memory Empty
mutations continue to isolate transform/storage regressions. Camera/Light data
decoding is not introduced. Separate Blender-written Empty fixtures compare native
world matrices, constructed parent-relative locals and reconstructed world
composition with saved transform oracles.
The integrated Scene oracles combine Mesh/Empty world/local matrices with
selected and parent-only hierarchy, shared geometry, render visibility,
fixed-child identifiers, mixed normals and indexed UVs at source scale `0.01`.
They compare every owning IR array after reader input release and require
exact equality under repeated and reversed-block loads; they do not close
the separate multi-scale native-to-USD unit evidence requirement.
Separate normal oracles compare single-Mesh Blender-written files, four packed
custom-normal files per version, two additional 5.2.2 custom polygon files,
and both independently constructed two-Mesh
fixtures. The 5.2.2 fixture repeats `Attribute` and `AttributeArray` addresses
with different payloads. Scene semantics resolve these only under the
Mesh ownership/reference contract; the reader's global `BuildPointerMap`
still fails with `BLEND_POINTER_DUPLICATE`. Their
[storage and reconstruction evidence](../../tests/fixtures/native-normals/README.md#boundary-limitations)
establishes the tested dense storage families, not every possible Mesh or
Blender version. The two-Mesh `constant.blend` files additionally compare
flat normals; the 5.2.2 file proves constant boolean `AttributeSingle`
storage. Single-flag AttributeArrays have synthetic evidence only.
Other named normal representations remain explicitly unsupported.
The native matrix tolerance is `2e-5 * (1 + abs(expected))` per component;
the decoder keeps strict finite/affine validation. The Stage and authoring
tables above record the composed importer and multi-scale
native-to-USD evidence separately.

# Diagnostics

§1 and §2 describe the record and families from
[DESIGN_POLICY.md §12](../design/DESIGN_POLICY.md#12-diagnostics), recorded
here so later code lands into an agreed shape; §3 lists implemented codes.

## 1. The record

```cpp
namespace blend {
enum class Severity {
    Fatal,          // nothing is authored
    Warning,        // authored, but not as the source meant
    Unsupported,    // read and reported, not authored
};

struct Diagnostic {
    std::string code;                    // BLEND_<FAMILY>_<EVENT>
    Severity severity;
    std::string message;
    std::optional<uint64_t> byteOffset;
    std::optional<uint32_t> blockIndex;
    std::string datablock;               // source name, when one applies
    bool recoverable;
};

template <class T> class Result;         // a value and its recoverable
                                         // diagnostics, or one fatal diagnostic
}
```

Tests assert codes, never messages. The file format reports fatal diagnostics
as `TF_RUNTIME_ERROR` and the rest as `TF_WARN`, and authors what it can.

## 2. Families (intended)

| Family | Owner | Covers |
| --- | --- | --- |
| `BLEND_HEADER_*` | `blendFile` | magic, pointer size, endianness, version |
| `BLEND_COMPRESSION_*` | `blendFile` | unsupported compression, corrupt stream, size and ratio limits |
| `BLEND_BLOCK_*` | `blendFile` | truncated or oversized blocks, unknown codes, missing `ENDB` |
| `BLEND_DNA_*` | `blendFile` | malformed SDNA, invalid indices, missing members |
| `BLEND_POINTER_*` | `blendFile` | unresolved old pointers, cycles |
| `BLEND_SCENE_*` | `blendScene` | missing scene, linked libraries, unsupported object types, evaluated data not applied |
| `BLEND_MESH_*` | `blendScene` | invalid topology, missing layers |
| `BLEND_MATERIAL_*` | `blendScene` | unsupported nodes and inputs |
| `BLEND_IMAGE_*` | `blendScene` | packed, absolute, generated, missing images |
| `BLEND_NAME_*` | `blendScene` | invalid UTF-8, collisions |
| `BLEND_HOST_*` | `blendHost` | Blender not configured, process failure, timeout, invalid interchange |
| `BLEND_USD_*` | `usdBlendFileFormat` | authoring failures |
| `BLEND_IO_*`, `BLEND_INSPECT_*` | `blend_inspect` | file opening, CLI arguments and exception boundary |

## 3. Implemented codes

Codes are fatal and non-recoverable unless the table specifies otherwise.
Block enumeration also returns recoverable `Unsupported` diagnostics for
unknown codes; unresolved pointers return recoverable `Warning` diagnostics.
Fixture paths are relative to
`plugins/usdBlendFileFormat/tests/fixtures/`.

| Code | Severity | Recoverable | Raised when | Evidence |
| --- | --- | --- | --- | --- |
| `BLEND_HEADER_TRUNCATED` | Fatal | no | fewer than twelve legacy or seventeen format-1 header bytes | `truncated.blend`; all short header prefixes, including complete short gzip members and Zstandard frames, in `blendFile.header` |
| `BLEND_HEADER_READ_FAILED` | Fatal | no | the source reports enough bytes but its read fails | `ShortSource` in `blendFile.header` |
| `BLEND_HEADER_MAGIC` | Fatal | no | the seven-byte signature differs | `invalid.blend` |
| `BLEND_HEADER_POINTER_SIZE` | Fatal | no | a legacy pointer marker is neither `_` nor `-` | `pointer_size.blend` |
| `BLEND_HEADER_ENDIANNESS` | Fatal | no | a byte-order marker is neither `v` nor `V` | `endianness.blend` |
| `BLEND_HEADER_VERSION` | Fatal | no | a version byte is not an ASCII digit | `version.blend`; legacy and format-1 cases in `blendFile.header` |
| `BLEND_HEADER_SIZE` | Fatal | no | an extended header declares a size other than 17 | `blendFile.header` |
| `BLEND_HEADER_FORMAT_VERSION` | Fatal | no | an extended header does not declare 8-byte pointers and format 01 | `blendFile.header` |
| `BLEND_COMPRESSION_DECODER` | Fatal | no | the decoder or full-file output cannot be allocated or configured | implemented; allocation failure unverified |
| `BLEND_COMPRESSION_TRUNCATED` | Fatal | no | compressed input ends before required probe output or before a full-stream member/frame finishes | short gzip and Zstandard prefixes, including gzip trailers, in `blendFile.header` |
| `BLEND_COMPRESSION_INVALID` | Fatal | no | the stream or checksum is corrupt, trailing data is invalid, or decoding makes no progress | invalid gzip method, flags and block type; intermediate and final CRC/ISIZE corruption; Zstandard block/checksum corruption and trailing garbage in `blendFile.header` |
| `BLEND_COMPRESSION_WINDOW_LIMIT` | Fatal | no | the Zstandard window exceeds the probe's 8 MiB or the full-file caller's window budget | oversized window in `blendFile.header` |
| `BLEND_COMPRESSION_INPUT_LIMIT` | Fatal | no | the probe exhausts 1 MiB before finding its header, or the full-file source exceeds the caller's input limit | empty members/frames, excessive metadata and full-file input boundaries in `blendFile.header` |
| `BLEND_COMPRESSION_READ_FAILED` | Fatal | no | a probe or full-file source read fails | gzip/Zstandard `CompressedReadFailure` and full-file initial/refill failures in `blendFile.header` |
| `BLEND_COMPRESSION_LIMITS` | Fatal | no | a full-file byte or ratio limit is zero, window log is outside 10 through 30, or compressed CLI input has no explicit limits | invalid limit configurations in `blendFile.header`; missing CLI limits in `blendInspect.cli` |
| `BLEND_COMPRESSION_OUTPUT_LIMIT` | Fatal | no | decoded bytes exceed the caller's output budget or the addressable vector size | uncompressed, gzip and Zstandard output boundaries in `blendFile.header`; address-space exhaustion unverified |
| `BLEND_COMPRESSION_RATIO_LIMIT` | Fatal | no | decoded bytes exceed source size times the caller's expansion ratio | gzip DEFLATE/Zstandard RLE bomb vectors and exact ratio boundaries in `blendFile.header` |
| `BLEND_BLOCK_LIMITS` | Fatal | no | the caller's block limit is zero or exceeds the maximum unsigned 32-bit index | invalid budgets in `blendFile.header` |
| `BLEND_BLOCK_COMPRESSED` | Fatal | no | block enumeration or the importer receives gzip or Zstandard bytes without an agreed production decompression policy | encoded synthetic containers in `blendFile.header`; compressed importer cases in `test_stage.py` |
| `BLEND_BLOCK_COUNT_LIMIT` | Fatal | no | the next block would exceed the caller's budget, or the record vector exceeds its addressable size | exact count boundary in `blendFile.header`; address-space exhaustion unverified |
| `BLEND_BLOCK_TRUNCATED` | Fatal | no | insufficient bytes remain for the selected block-header layout | all short block-header prefixes in `blendFile.header` |
| `BLEND_BLOCK_READ_FAILED` | Fatal | no | a bounded block-header read fails | source failures at successive block headers in `blendFile.header` |
| `BLEND_BLOCK_NEGATIVE_LENGTH` | Fatal | no | the signed stored payload length is negative | legacy and format-1 sign-bit cases in `blendFile.header` |
| `BLEND_BLOCK_NEGATIVE_SDNA` | Fatal | no | the signed stored SDNA index is negative | all block layouts in `blendFile.header` |
| `BLEND_BLOCK_NEGATIVE_COUNT` | Fatal | no | the signed stored element count is negative | legacy and format-1 sign-bit cases in `blendFile.header` |
| `BLEND_BLOCK_SIZE` | Fatal | no | a declared payload exceeds the source or an ID/value-view range exceeds the supplied decoded bytes | short payloads, maximum signed lengths, raw ID range errors and every short value-view span in `blendFile.header` |
| `BLEND_BLOCK_ENDB` | Fatal | no | `ENDB` declares a nonempty payload | synthetic containers in `blendFile.header` |
| `BLEND_BLOCK_TRAILING` | Fatal | no | bytes or another block follow `ENDB` | trailing byte and duplicate `ENDB` cases in `blendFile.header` |
| `BLEND_BLOCK_MISSING_ENDB` | Fatal | no | the source ends at a block boundary without `ENDB` | header-only and terminal payload truncations in `blendFile.header` |
| `BLEND_BLOCK_ALLOCATION` | Fatal | no | block records or diagnostics cannot be allocated | implemented; allocation failure unverified |
| `BLEND_BLOCK_UNKNOWN_CODE` | Unsupported | yes | a code is not recognized; its record is retained and its payload skipped | synthetic unknown block in `blendFile.header` |
| `BLEND_DNA_LAYOUT` | Fatal | no | the supplied header has an invalid pointer size or byte order | invalid layouts in `blendFile.header` |
| `BLEND_DNA_TRUNCATED` | Fatal | no | an integer, tag, string terminator, type length or alignment padding is incomplete | short SDNA payload prefixes in `blendFile.header` |
| `BLEND_DNA_SECTION` | Fatal | no | an SDNA section tag differs from its required identifier | every section tag corrupted in `blendFile.header` |
| `BLEND_DNA_COUNT` | Fatal | no | a table or member count exceeds remaining records or the index range | oversized NAME/TYPE/STRC and member counts in `blendFile.header` |
| `BLEND_DNA_INDEX` | Fatal | no | a structure type, member type/name, raw ID SDNA index or value-view block/element/array index is out of range | each schema index kind, raw ID indices and value-view indices in `blendFile.header` |
| `BLEND_DNA_NAME` | Fatal | no | a schema name/declarator is malformed, or raw ID.name lacks a bounded terminator or two-byte prefix | invalid identifiers, arrays, function pointers and raw ID names in `blendFile.header` |
| `BLEND_DNA_DUPLICATE` | Fatal | no | a type name, structure type or member base name is duplicated | each duplicate kind in `blendFile.header` |
| `BLEND_DNA_SIZE` | Fatal | no | schema sizes overflow or differ from TLEN, or an ID/value-view count/length/member range is invalid | schema array/length errors, raw ID count/size/member-range errors and value-view count/range errors in `blendFile.header` |
| `BLEND_DNA_MEMBER` | Fatal | no | a raw ID lacks a required member, or a value-view member is absent or selected on a pointer/array/non-struct | wrong pointer/member types and invalid value-view member access in `blendFile.header` |
| `BLEND_DNA_VALUE` | Fatal | no | a value-view pointer or numeric read has an incompatible scalar type, width or shape | wrong numeric types, pointers and unselected arrays in `blendFile.header` |
| `BLEND_DNA_TRAILING` | Fatal | no | bytes remain after the STRC records | trailing payload byte in `blendFile.header` |
| `BLEND_DNA_ALLOCATION` | Fatal | no | the owning schema or raw datablock records cannot be allocated | implemented; allocation failure unverified |
| `BLEND_DNA_BLOCK` | Fatal | no | the importer or inspection tool finds missing or multiple DNA1 blocks | `test_stage.py`; modified empty-scene containers in `blendInspect.cli` |
| `BLEND_POINTER_DUPLICATE` | Fatal | no | the global reader map finds duplicate nonzero target addresses, or scene semantics cannot prove the narrow 5.x Mesh-owned Attribute/AttributeArray/AttributeSingle exception | synthetic duplicate DATA blocks in `blendFile.header`; strict mapping and wrong/non-Mesh owners, unreferenced targets, other SDNA types, duplicate IDs and legacy-header mutations of 5.2.2 `multi.blend` in `blendScene.meshBoundaries`; in-memory AttributeSingle collisions and wrong storage discriminators from `constant.blend`; metadata collisions are excluded |
| `BLEND_POINTER_UNRESOLVED` | Warning | yes | an exact nonzero old address has no reference-target block; resolution returns null | absent and interior keys in `blendFile.header` |
| `BLEND_POINTER_LIMIT` | Fatal | no | the input block count exceeds unsigned 32-bit indices | implemented; excessive allocation/count unverified |
| `BLEND_POINTER_ALLOCATION` | Fatal | no | pointer entries or the unresolved diagnostic cannot be allocated | implemented; allocation failure unverified |
| `BLEND_SCENE_GLOBAL_INVALID` | Fatal | no | GLOB is missing, duplicated or not one FileGlobal | synthetic GLOB absence/count/type cases in `blendScene.ir` |
| `BLEND_SCENE_ACTIVE_MISSING` | Fatal | no | FileGlobal.curscene is null; no implicit Scene fallback | Scene-only `empty.blend` and synthetic null pointers in `blendScene.ir` |
| `BLEND_SCENE_REFERENCE_INVALID` | Fatal | no | a required Scene/Collection/list/Object reference, nonzero Object parent/data/instance, target code/type/count or semantic member shape is incompatible; opt-in Object values also reject missing type-required data or a null enabled instance | null/absent/interior/metadata pointers, wrong pointer types and targets, synthetic parent/data/value/instance errors and corpus pointer/flag mutations in `blendScene.ir` |
| `BLEND_SCENE_OBJECT_TYPE_UNSUPPORTED` | Fatal | no | opt-in Object value reading finds a source type without a verified data mapping | positive unmapped and negative short types in `blendScene.ir`; native decoding preserves selection failures |
| `BLEND_SCENE_OBJECT_DATA_UNSUPPORTED` | Unsupported | yes | native decoding preserves a known Object's unsupported data as an Empty, without decoding or fabricating geometry | all mapped non-Mesh kinds and Image Empty across four synthetic layouts in `blendScene.ir`; unchanged Camera/Light corpus inputs; Blender 5.2.2 mixed-kind oracle in `blendScene.objectFallbacks` and registered-plugin tests |
| `BLEND_SCENE_INSTANCE_UNSUPPORTED` | Fatal | no | native decoding reaches enabled Collection instancing after graph validation | valid instance target across four synthetic layouts in `blendScene.ir`; graph cycles retain `BLEND_SCENE_CYCLE` |
| `BLEND_SCENE_TRANSFORM_UNSUPPORTED` | Fatal | no | native decoding reaches an unknown rotation mode, transform flags other than cached negative-handedness bit 2, or non-ordinary parenting | unknown modes, every other non-instance flag with/without bit 2, and parenting mode across four synthetic layouts in `blendScene.ir`; bit 2 does not override channels |
| `BLEND_SCENE_TRANSFORM_INVALID` | Fatal | no | native source transform storage has an incompatible scalar/array shape, nonfinite active channels, non-affine parent inverse or construction overflow; parent-relative construction receives nonfinite/non-affine worlds or overflows | malformed shapes, NaN/infinity in Euler/Quaternion/Axis-Angle channels, projective parent inverse and overflowing source parent matrices across four synthetic layouts; helper input and initial/elimination/translation overflow cases in `blendScene.ir`; inactive rotation channels are not interpreted |
| `BLEND_SCENE_TRANSFORM_SINGULAR` | Fatal | no | parent-relative construction finds a zero parent linear row or a row-scaled pivot at most eight double-precision epsilons | zero-scale parents, dependent nontrivial rows and numerically singular parents in `blendScene.ir`; singular roots/children with invertible parents remain valid |
| `BLEND_SCENE_EVALUATION_UNAPPLIED` | Unsupported | yes | native decoding uses source transform/geometry values with nonnull animation, constraint or Mesh modifier endpoints; evaluation contents are not followed | independent animation, constraint endpoints and modifier presence with contextual diagnostics across four synthetic layouts in `blendScene.ir` |
| `BLEND_NAME_INVALID_UTF8` | Warning | yes | display-name validation finds an ill-formed UTF-8 subpart; raw source bytes are retained and display uses U+FFFD | valid scalar boundaries and overlong/surrogate/out-of-range/truncated sequences in `blendScene.naming`; native Object warnings with exact offsets/indices across four layouts in `blendScene.ir` |
| `BLEND_NAME_DUPLICATE` | Fatal | no | sibling Objects or UV maps within one Mesh have identical raw source names, so a total source-byte ordering is impossible | standalone duplicate rejection in `blendScene.naming`, contextual native rejection across four layouts in `blendScene.ir`, duplicate UV maps in `usdBlend.authoring` |
| `BLEND_NAME_RENDER_UV_INVALID` | Fatal | no | UV naming receives more than one active-render map | multiple render maps in `usdBlend.authoring` |
| `BLEND_NAME_ALLOCATION` | Fatal | no | Object identifier or display-text allocation fails or exceeds container capacity | implemented; allocation failure injection unverified |
| `BLEND_MESH_REFERENCE_INVALID` | Fatal | no | a required Mesh storage pointer is null, absent, interior or resolves to a non-DATA block | offset/name/value pointers and non-DATA arrays in both synthetic storage forms across four layouts in `blendScene.ir` |
| `BLEND_MESH_STORAGE_INVALID` | Fatal | no | Mesh counts, member shapes, record/array lengths/counts, attribute names, boolean values, AttributeArray single flags, legacy geometry aliases/empty pointers or UV selectors are invalid | negative/excessive counts, logical AttributeArray sizes, exact one-value constant payload lengths, single flags other than zero/one, SDNA record counts, MLoopUV record types/coordinate shapes, legacy record/type/member-shape, mismatched/duplicate/wrong-domain geometry aliases and zero-domain pointer mutations, unterminated/duplicate names, boolean and UV selection mutations in `blendScene.ir` |
| `BLEND_MESH_STORAGE_UNSUPPORTED` | Fatal | no | version/core attribute type/domain, missing required storage, mixed/external CustomData, flagged layers, special Attribute storage other than Array/Single or non-corner MLoopUV layers are outside the decoder | version/type/domain, flagged/special Attribute storage, flagged 3.3 geometry aliases and wrong-domain MLoopUV mutations in `blendScene.ir`; unverified-version mutations of the 3.3.21 fixture retain exact fatal Mesh context and no partial IR in `blendScene.legacyMeshStorage`; mixed/external rejection implemented but unverified |
| `BLEND_MESH_TOPOLOGY_INVALID` | Fatal | no | face/corner counts disagree, modern offsets or legacy polygon ranges do not cover valid polygons, a corner vertex/consumed edge is out of range, or shared corner-edge uses disagree on endpoints | nonzero first, decreasing/negative/short/out-of-range/final offsets, noncontiguous/short/excessive/uncovered legacy ranges, signed/unsigned legacy index overflow, negative/out-of-range vertex/edge indices and inconsistent shared-edge endpoints in `blendScene.ir`; signed negative/overflow/out-of-domain mutations of the unchanged 3.3.21 storage in `blendScene.legacyMeshStorage` |
| `BLEND_MESH_VALUE_INVALID` | Fatal | no | a source position or UV component is nonfinite | NaN/infinity positions and UVs in both storage forms across four layouts in `blendScene.ir` |
| `BLEND_MESH_NORMALS_UNSUPPORTED` | Fatal | no | custom normal names, types or domains do not match the supported packed corner representation, or Blender 3.3 Mesh flags/custom normals are outside its tested default/auto-smooth modes | wrong named/type-41 mutations in `blendScene.ir`; valid unnamed type-41 and modern corner short pairs decode in 4.5/5.x; unsupported 3.3 Mesh flags and packed layers fail contextually across four synthetic layouts and saved-flag mutation in `blendScene.legacyMeshStorage` |
| `BLEND_MESH_NORMALS_INVALID` | Fatal | no | a polygon normal, corner direction, angle-weighted normal sum or custom reference space is zero or nonfinite, or an enabled Blender 3.3 smoothing angle is nonfinite or outside zero to stored float32 pi | collapsed source points and cancelling smooth face normals in both storage forms across four layouts in `blendScene.ir`; negative, NaN, infinity and immediately-above-pi legacy angles retain exact fatal context; custom-space rejection implemented, isolated reference-space degeneration unverified |
| `BLEND_MESH_EMPTY` | Warning | yes | a source Mesh has no polygons; points and empty topology are retained | one contextual warning for a shared empty Mesh in both storage forms across four layouts in `blendScene.ir` |
| `BLEND_MESH_EVALUATION_UNAPPLIED` | Unsupported | yes | a source Mesh has shape-key data; source positions are used without following or evaluating keys | nonnull key mutation leaves Mesh values unchanged in both storage forms across four layouts in `blendScene.ir` |
| `BLEND_SCENE_LINKED_UNSUPPORTED` | Fatal | no | a selected Scene or reached Collection/Object/data ID has nonzero ID.lib; external data is not followed | synthetic linked Scene/Collection/Object/data IDs in `blendScene.ir` |
| `BLEND_SCENE_NAME_INVALID` | Fatal | no | a selected Scene/Collection/Object/data ID.name is not a terminated, correctly prefixed one-byte char array | synthetic invalid Scene/Collection/Object/data prefixes and Scene/data terminators in `blendScene.ir` |
| `BLEND_SCENE_LIMITS` | Fatal | no | Object selection receives a zero visit or depth limit | both missing limits in `blendScene.ir` |
| `BLEND_SCENE_VISIT_LIMIT` | Fatal | no | the next Collection/list node or unique Object/data target, including parent-only Objects and their data, or recursively reached instance Collection content would exceed the caller's visit limit | exact and one-smaller budgets, shared-data, Object/data and recursive instance/Collection visit deduplication across all four layouts in `blendScene.ir` |
| `BLEND_SCENE_DEPTH_LIMIT` | Fatal | no | the next membership or recursively instanced Collection, or unfinished Object parent expansion, would exceed the caller's active stack/chain limit | two-level, 256-child and 256-parent chains plus recursive instance depth in `blendScene.ir` |
| `BLEND_SCENE_LIST_INVALID` | Fatal | no | ListBase endpoints, prev backlinks, terminal last or exclusive list-node ownership disagree | endpoint/backlink/last and shared-node cases across all four layouts in `blendScene.ir` |
| `BLEND_SCENE_CYCLE` | Fatal | no | a next chain repeats a node, a child or instanced Collection refers to an active Collection, or an Object parent refers to an active ancestor | list and self/ancestor membership, recursive-instance and Object cycles across all four layouts; self-parent corpus mutations in `blendScene.ir` |
| `BLEND_SCENE_ALLOCATION` | Fatal | no | scene selection, Collection traversal, Object validation or native Scene decoding cannot allocate | implemented; allocation failure injection unverified |
| `BLEND_SCENE_UNIT_SCALE_INVALID` | Fatal | no | the source unit scale is zero, negative or nonfinite | `UnitConversion` constructor and saved Scene selection cases in `blendScene.ir` |
| `BLEND_SCENE_UNIT_VALUE_INVALID` | Fatal | no | a source distance is nonfinite or its conversion to meters overflows | scalar, position and translation cases in `blendScene.ir` |
| `BLEND_SCENE_UNIT_TRANSFORM_INVALID` | Fatal | no | unit conversion receives a non-affine mesh/empty world matrix | projective matrix in `blendScene.ir` |
| `BLEND_HEADER_OPEN_FAILED` | Fatal | no | ArResolver cannot open the resolved asset | implemented; fixture unverified |
| `BLEND_USD_AUTHORING_FAILED` | Fatal | no | a temporary stage or requested USD schema/value cannot be authored, or OpenUSD reports an authoring error | success-path schema/setter checks implemented; failure injection unverified |
| `BLEND_USD_ALLOCATION` | Fatal | no | authoring storage cannot be allocated or exceeds container capacity | implemented; allocation failure injection unverified |
| `BLEND_USD_IDENTIFIER_INVALID` | Fatal | no | a supplied Object identifier differs from the native naming contract | malformed identifier in `usdBlend.authoring` |
| `BLEND_USD_TOPOLOGY_INVALID` | Fatal | no | IR polygon counts, corner indices, point references or face-varying normal lengths disagree | short polygons, negative indices and normal-size mismatch in `usdBlend.authoring` |
| `BLEND_USD_UV_INVALID` | Fatal | no | IR face-varying UV index count or value references are invalid | short indices and out-of-range UV references in `usdBlend.authoring` |
| `BLEND_USD_VALUE_INVALID` | Fatal | no | a point, normal or UV component is nonfinite or outside finite float range | nonfinite/oversized geometry and the exact/one-above float maximum in `usdBlend.authoring` |
| `BLEND_USD_READ_FAILED` | Fatal | no | a C++ exception reaches the importer boundary | implemented; fixture unverified |
| `BLEND_IO_OPEN` | Fatal | no | the tool cannot open its input path | missing file in `blendInspect.cli` |
| `BLEND_INSPECT_USAGE` | Fatal | no | CLI arguments are invalid or incomplete | invalid options, decimal values and argument counts in `blendInspect.cli`; exit status 2 |
| `BLEND_INSPECT_MEMORY` | Fatal | no | a C++ allocation exception reaches the CLI boundary | implemented; allocation failure injection unverified |
| `BLEND_INSPECT_ERROR` | Fatal | no | another C++ exception reaches the CLI boundary | implemented; exception injection unverified |

`ReadDna` diagnostics use DNA1-payload-relative byte offsets, not file offsets,
and have no block index. A container caller may attach its block context; see
the [schema boundary](../design/BLEND_CONTRACT.md#71-schema-decoding-boundary).

`ListDatablocks` errors instead attach the ID payload's uncompressed file
offset and block index. Pointer-map duplicate errors identify the later
reference-target block; `Resolve` warnings have no referring-block context.
See the [raw boundaries](../design/BLEND_CONTRACT.md#81-pointer-map-boundary).

`DnaValueView` errors attach the current view's uncompressed file offset and
selected block index. Binding failures use the block payload offset, or zero
when no such block exists. No Scene/datablock context is inferred; see the
[borrowed value boundary](../design/BLEND_CONTRACT.md#83-borrowed-sdna-value-boundary).

`SelectScene` semantic diagnostics attach the referring GLOB or selected
target's uncompressed payload offset and block index. A missing GLOB or
allocation failure has no source context. Required-reference resolution
promotes an unresolved pointer to fatal `BLEND_SCENE_REFERENCE_INVALID` with
GLOB context; it does not return a recoverable pointer warning and a selection.
Reader binding/member and pointer-map failures retain their existing context.
See the [selection boundary](../design/DESIGN_POLICY.md#522-saved-scene-selection-boundary).

`SelectSceneObjects` preserves selection/reader failures and returns fatal
semantic traversal errors, never partial output or recoverable unresolved
reference warnings. Invalid targets use target context; unresolved required
references use the referring Scene, Collection or list node. Collection cycle
and depth failures use the referring Collection. Visit failures identify the
Collection referrer or current list node; invalid limits and allocation
failures have no context. Names are raw output provenance, not diagnostic
datablock labels. See the
[membership boundary](../design/DESIGN_POLICY.md#523-saved-collection-membership-boundary).

Parent validation likewise returns no partial selection. Unresolved nonzero
parents, cycles, parent-depth failures and parent-only visit failures use the
referring Object's context; invalid parent targets use target context. Null
parents are valid roots. Reader failures retain their codes and offsets. See
the [parent-reference boundary](../design/DESIGN_POLICY.md#524-saved-object-parent-reference-boundary).

`ObjectIdentifiers` returns names aligned with the input Scene IR and no partial
identifier vector on fatal errors. `NameForDisplay` and identifier warnings have
raw name context but no source offsets; `DecodeScene` attaches each affected
selected Object's payload offset and block index. Duplicate sibling names fail
with selected Object context rather than an enumeration-based identifier
tie-break. Allocation failures have no source context. Invalid immediate IR
parent/Mesh indices use `BLEND_SCENE_REFERENCE_INVALID`; parent-chain validation
remains the decoder's prerequisite. See the
[naming boundary](../design/NAMING_POLICY.md#41-native-object-naming-boundary).

Data validation returns no partial selection. Unresolved nonzero data pointers,
invalid pointer shapes and new-data visit exhaustion use referring Object
context; invalid/linked data targets use target context. A null data pointer
is allowed without Object type-specific policy. Reader failures retain their
codes and offsets. See the
[data-reference boundary](../design/DESIGN_POLICY.md#525-saved-object-data-reference-boundary).

The standalone `UnitConversion` helper throws `std::invalid_argument` for
invalid input or `std::overflow_error` for meter-conversion overflow, with the
stable code followed by a colon at the start of `what()`. It does not return a
`Diagnostic` record or know source offsets/datablocks. `DecodeScene` translates
unit conversion failures into fatal, non-recoverable diagnostics with Object or Mesh
context and no fallback Scene; an overflowing native parent translation is
tested across four synthetic layouts. Scene unit validation retains selection
context. The uncompressed importer now forwards these native failures.

`ParentRelativeTransform` uses the same exception/code-prefix convention:
invalid finite/affine inputs and singular parents throw `std::invalid_argument`;
construction overflow throws `std::overflow_error`. It knows no source
offset, Object name or graph, and neither returns a `Diagnostic` record nor
changes the decoder's existing world-only output. A caller authoring an Object
must translate these failures with that Object's context and publish no partial
stage. There is no silent identity or hierarchy fallback. The importer
forwards authoring failures with the affected Object name.

`DecodeScene` first preserves the complete Object-value selection and recursive
graph validation boundary. Its transform failures identify the affected Object's
payload offset and block index; unit failures also carry its source name.
Source-evaluation presence diagnostics identify each decoded Object once,
including parent-only Objects, without following evaluation pointers. Mesh
Objects also report modifier presence; each shared decoded Mesh reports
shape-key presence once. Unsupported-data diagnostics likewise identify each
decoded Object once, with source name and exact Object block context, even
for parent-only Objects or shared data. Unknown types, linked or malformed
references, active instances and invalid/unsupported transforms remain fatal;
the Empty fallback cannot turn them into a successful Scene.
Mesh semantic failures identify the referring or
invalid storage target's payload offset and block index; source numeric arrays
and record counts are validated without publishing partial IR. Reader errors
retain their original codes and value-view offsets.
Allocation failures have no context. See the
[native decoding boundary](../design/DESIGN_POLICY.md#527-native-scene-decoding-boundary)
and [Mesh storage boundary](../design/DESIGN_POLICY.md#528-native-mesh-storage-boundary).

The tool prints codes and severity to stderr and preserves available block
and datablock context. DNA1-relative offsets are translated to decoded file
offsets for its display; block and ID offsets already use decoded bytes.
Full-stream diagnostics retain their library byte coordinate. Recoverable
unsupported-block diagnostics do not change a successful CLI exit status.

The importer similarly preserves byte, block and datablock context through
`TF_RUNTIME_ERROR` and `TF_WARN`. It translates DNA1-relative error offsets
into input-file coordinates and transfers no target-layer content on failure.
`CanRead` remains a header-identification probe and can accept an input that a
full or metadata-only scene read rejects.

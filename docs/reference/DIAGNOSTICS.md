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
| `BLEND_BLOCK_COMPRESSED` | Fatal | no | block enumeration receives gzip or Zstandard bytes instead of an uncompressed source | encoded synthetic containers in `blendFile.header` |
| `BLEND_BLOCK_COUNT_LIMIT` | Fatal | no | the next block would exceed the caller's budget, or the record vector exceeds its addressable size | exact count boundary in `blendFile.header`; address-space exhaustion unverified |
| `BLEND_BLOCK_TRUNCATED` | Fatal | no | insufficient bytes remain for the selected block-header layout | all short block-header prefixes in `blendFile.header` |
| `BLEND_BLOCK_READ_FAILED` | Fatal | no | a bounded block-header read fails | source failures at successive block headers in `blendFile.header` |
| `BLEND_BLOCK_NEGATIVE_LENGTH` | Fatal | no | the signed stored payload length is negative | legacy and format-1 sign-bit cases in `blendFile.header` |
| `BLEND_BLOCK_NEGATIVE_SDNA` | Fatal | no | the signed stored SDNA index is negative | all block layouts in `blendFile.header` |
| `BLEND_BLOCK_NEGATIVE_COUNT` | Fatal | no | the signed stored element count is negative | legacy and format-1 sign-bit cases in `blendFile.header` |
| `BLEND_BLOCK_SIZE` | Fatal | no | a declared payload exceeds the source or an ID range exceeds the supplied decoded bytes | short payloads, maximum signed lengths and raw ID range errors in `blendFile.header` |
| `BLEND_BLOCK_ENDB` | Fatal | no | `ENDB` declares a nonempty payload | synthetic containers in `blendFile.header` |
| `BLEND_BLOCK_TRAILING` | Fatal | no | bytes or another block follow `ENDB` | trailing byte and duplicate `ENDB` cases in `blendFile.header` |
| `BLEND_BLOCK_MISSING_ENDB` | Fatal | no | the source ends at a block boundary without `ENDB` | header-only and terminal payload truncations in `blendFile.header` |
| `BLEND_BLOCK_ALLOCATION` | Fatal | no | block records or diagnostics cannot be allocated | implemented; allocation failure unverified |
| `BLEND_BLOCK_UNKNOWN_CODE` | Unsupported | yes | a code is not recognized; its record is retained and its payload skipped | synthetic unknown block in `blendFile.header` |
| `BLEND_DNA_LAYOUT` | Fatal | no | the supplied header has an invalid pointer size or byte order | invalid layouts in `blendFile.header` |
| `BLEND_DNA_TRUNCATED` | Fatal | no | an integer, tag, string terminator, type length or alignment padding is incomplete | short SDNA payload prefixes in `blendFile.header` |
| `BLEND_DNA_SECTION` | Fatal | no | an SDNA section tag differs from its required identifier | every section tag corrupted in `blendFile.header` |
| `BLEND_DNA_COUNT` | Fatal | no | a table or member count exceeds remaining records or the index range | oversized NAME/TYPE/STRC and member counts in `blendFile.header` |
| `BLEND_DNA_INDEX` | Fatal | no | a structure type, member type/name or raw ID SDNA index is out of range | each schema index kind and raw ID indices in `blendFile.header` |
| `BLEND_DNA_NAME` | Fatal | no | a schema name/declarator is malformed, or raw ID.name lacks a bounded terminator or two-byte prefix | invalid identifiers, arrays, function pointers and raw ID names in `blendFile.header` |
| `BLEND_DNA_DUPLICATE` | Fatal | no | a type name, structure type or member base name is duplicated | each duplicate kind in `blendFile.header` |
| `BLEND_DNA_SIZE` | Fatal | no | schema sizes overflow or differ from TLEN, or an ID count/length/member range is invalid | schema array/length errors and raw ID count/size/member-range errors in `blendFile.header` |
| `BLEND_DNA_MEMBER` | Fatal | no | a raw ID lacks an embedded ID or bounded char name array of the required type | wrong pointer/member types in `blendFile.header` |
| `BLEND_DNA_TRAILING` | Fatal | no | bytes remain after the STRC records | trailing payload byte in `blendFile.header` |
| `BLEND_DNA_ALLOCATION` | Fatal | no | the owning schema or raw datablock records cannot be allocated | implemented; allocation failure unverified |
| `BLEND_DNA_BLOCK` | Fatal | no | the tool finds no DNA1 block or more than one | modified empty-scene containers in `blendInspect.cli` |
| `BLEND_POINTER_DUPLICATE` | Fatal | no | two reference-target blocks have the same nonzero old address | synthetic duplicate DATA blocks in `blendFile.header`; metadata collisions are excluded |
| `BLEND_POINTER_UNRESOLVED` | Warning | yes | an exact nonzero old address has no reference-target block; resolution returns null | absent and interior keys in `blendFile.header` |
| `BLEND_POINTER_LIMIT` | Fatal | no | the input block count exceeds unsigned 32-bit indices | implemented; excessive allocation/count unverified |
| `BLEND_POINTER_ALLOCATION` | Fatal | no | pointer entries or the unresolved diagnostic cannot be allocated | implemented; allocation failure unverified |
| `BLEND_SCENE_UNIT_SCALE_INVALID` | Fatal | no | the source unit scale is zero, negative or nonfinite | `UnitConversion` constructor cases in `blendScene.ir` |
| `BLEND_SCENE_UNIT_VALUE_INVALID` | Fatal | no | a source distance is nonfinite or its conversion to meters overflows | scalar, position and translation cases in `blendScene.ir` |
| `BLEND_SCENE_UNIT_TRANSFORM_INVALID` | Fatal | no | unit conversion receives a non-affine mesh/empty world matrix | projective matrix in `blendScene.ir` |
| `BLEND_HEADER_OPEN_FAILED` | Fatal | no | ArResolver cannot open the resolved asset | implemented; fixture unverified |
| `BLEND_USD_AUTHORING_FAILED` | Fatal | no | the temporary stage cannot be created | implemented; fixture unverified |
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

The standalone `UnitConversion` helper throws `std::invalid_argument` for
invalid input or `std::overflow_error` for meter-conversion overflow, with the
stable code followed by a colon at the start of `what()`. It does not return a
`Diagnostic` record or know source offsets/datablocks. A future scene decoder
must translate these failures into fatal, non-recoverable diagnostics and
attach Scene/object context, with no fallback geometry. These failures are
library-tested, not yet reachable through the header-only importer.

The tool prints codes and severity to stderr and preserves available block
and datablock context. DNA1-relative offsets are translated to decoded file
offsets for its display; block and ID offsets already use decoded bytes.
Full-stream diagnostics retain their library byte coordinate. Recoverable
unsupported-block diagnostics do not change a successful CLI exit status.

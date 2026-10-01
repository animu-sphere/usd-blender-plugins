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

## 3. Implemented codes

Codes are fatal and non-recoverable unless the table specifies otherwise.
Block enumeration also returns recoverable `Unsupported` diagnostics for
unknown codes. Fixture paths are relative to
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
| `BLEND_COMPRESSION_LIMITS` | Fatal | no | a full-file byte or ratio limit is zero, or window log is outside 10 through 30 | invalid limit configurations in `blendFile.header` |
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
| `BLEND_BLOCK_SIZE` | Fatal | no | a nonnegative declared payload length exceeds the remaining bytes | short payloads and maximum signed lengths in `blendFile.header` |
| `BLEND_BLOCK_ENDB` | Fatal | no | `ENDB` declares a nonempty payload | synthetic containers in `blendFile.header` |
| `BLEND_BLOCK_TRAILING` | Fatal | no | bytes or another block follow `ENDB` | trailing byte and duplicate `ENDB` cases in `blendFile.header` |
| `BLEND_BLOCK_MISSING_ENDB` | Fatal | no | the source ends at a block boundary without `ENDB` | header-only and terminal payload truncations in `blendFile.header` |
| `BLEND_BLOCK_ALLOCATION` | Fatal | no | block records or diagnostics cannot be allocated | implemented; allocation failure unverified |
| `BLEND_BLOCK_UNKNOWN_CODE` | Unsupported | yes | a code is not recognized; its record is retained and its payload skipped | synthetic unknown block in `blendFile.header` |
| `BLEND_HEADER_OPEN_FAILED` | Fatal | no | ArResolver cannot open the resolved asset | implemented; fixture unverified |
| `BLEND_USD_AUTHORING_FAILED` | Fatal | no | the temporary stage cannot be created | implemented; fixture unverified |
| `BLEND_USD_READ_FAILED` | Fatal | no | a C++ exception reaches the importer boundary | implemented; fixture unverified |

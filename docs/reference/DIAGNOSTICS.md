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

All current codes are fatal and non-recoverable. Fixture paths are relative
to `plugins/usdBlendFileFormat/tests/fixtures/`.

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
| `BLEND_COMPRESSION_DECODER` | Fatal | no | the decoder cannot be allocated or configured | implemented; allocation failure unverified |
| `BLEND_COMPRESSION_TRUNCATED` | Fatal | no | compressed input ends before the required header output and the current member or frame is incomplete | short gzip and Zstandard input prefixes in `blendFile.header` |
| `BLEND_COMPRESSION_INVALID` | Fatal | no | the header stream is corrupt or decoding makes no progress | invalid gzip method, flags and block type; corrupt intermediate member CRC/size; invalid Zstandard block type in `blendFile.header` |
| `BLEND_COMPRESSION_WINDOW_LIMIT` | Fatal | no | the Zstandard decoder window exceeds 8 MiB | oversized window in `blendFile.header` |
| `BLEND_COMPRESSION_INPUT_LIMIT` | Fatal | no | 1 MiB of input produces too few header bytes | empty gzip member and Zstandard frame sequences; excessive gzip metadata in `blendFile.header` |
| `BLEND_COMPRESSION_READ_FAILED` | Fatal | no | a compressed-source read fails | gzip and Zstandard `CompressedReadFailure` cases in `blendFile.header` |
| `BLEND_HEADER_OPEN_FAILED` | Fatal | no | ArResolver cannot open the resolved asset | implemented; fixture unverified |
| `BLEND_USD_AUTHORING_FAILED` | Fatal | no | the temporary stage cannot be created | implemented; fixture unverified |
| `BLEND_USD_READ_FAILED` | Fatal | no | a C++ exception reaches the importer boundary | implemented; fixture unverified |

# Diagnostics

As of 2026-10-01 no diagnostic is implemented. §1 and §2 are the intended
record and families from
[DESIGN_POLICY.md §12](../design/DESIGN_POLICY.md#12-diagnostics), recorded
here so the first code lands into an agreed shape; §3 lists the codes that
exist, and is empty.

## 1. The record (intended)

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

None yet. Each code is added here by the change that first raises it, as a row
of code, severity, recoverable flag, when it is raised, and the fixture that
proves it.

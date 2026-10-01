# Blend contract

> Status: **proposed**, 2026-10-01. This document defines intended behavior;
> implemented behavior belongs to the
> [capability matrix](../reference/CAPABILITY_MATRIX.md).
>
> This document owns how `.blend` bytes are read — the container, SDNA and
> blocks in `blendFile` — and what each source concept becomes in the Scene IR
> built by `blendScene`. On this area it wins over the design policy. What the
> Scene IR becomes in USD is [STAGE_CONTRACT.md](STAGE_CONTRACT.md).
>
> Format facts here are written from Blender's public developer documentation
> and observed files. Where a fact is not yet confirmed by a fixture it is
> marked, and confirming it is part of the Phase that reads it.

## 1. Scope

A `.blend` is not a fixed binary schema. Blender describes its persistent
structures with DNA (SDNA) and embeds that description in every file. The
reader therefore never hard-codes a Blender version's C structs: it reads the
file's own SDNA and looks members up by struct and member name.

```text
ByteSource
   ↓
decompression                       blendFile
   ↓
file header
   ↓
block headers   ──→ legacy layout | Blender 5 layout
   ↓
DNA1 block → SDNA (names, types, lengths, structs)
   ↓
datablock access by struct and member name
   ↓ ─────────────────────────────────────────────
ID graph reconstruction             blendScene
   ↓
Scene IR (USD basis, meters, identifiers)
```

## 2. Reading rules

These apply to every reader in `blendFile` and `blendScene`
([DESIGN_POLICY.md §10](DESIGN_POLICY.md#10-parser-strategy)):

1. **Bounded reads.** Every read is checked against the source size before it
   happens. A short read is a diagnostic, never undefined behavior.
2. **Checked arithmetic.** Every offset, size and `count × size` is computed
   with overflow checks.
3. **Validated sizes.** A block's declared length must fit in the remaining
   bytes. A struct read from a block must fit in the block, using the SDNA
   length of that struct in *this* file.
4. **Bounded allocation.** Allocation follows validated sizes. Decompressed
   output is limited by an explicit maximum, and the ratio of decompressed to
   compressed bytes is limited, against decompression bombs (BLEND-O5).
5. **Validated indices.** SDNA type, struct and name indices are checked
   against their tables before use. Every array index from the file is
   checked against its array.
6. **No pointer walking.** Old pointers stored in the file are keys into the
   pointer map (§8), never addresses. An old pointer with no block is a
   diagnostic, and the reference is treated as null.
7. **Bounded recursion.** ID graph traversal, collection nesting, parenting
   chains and node trees have a depth limit, and cycles are detected.
8. **Explicit byte order.** Integers are decoded with the file's declared
   endianness and pointer size, never the host's.
9. **No exceptions across boundaries.** Failures are diagnostics with stable
   codes ([reference/DIAGNOSTICS.md](../reference/DIAGNOSTICS.md)).

## 3. Byte sources

The lowest layer is an abstract byte source, so the reader works from a file,
memory, or an OpenUSD `ArAsset`, and is directly fuzzable:

```cpp
namespace blend {
class ByteSource {
public:
    virtual ~ByteSource() = default;
    virtual uint64_t Size() const = 0;
    virtual bool Read(uint64_t offset, std::span<std::byte> dst) = 0;
};
}
```

| Implementation | Owner | Use |
| --- | --- | --- |
| `FileByteSource` | `blendFile` | `blend_inspect`, tests |
| `MemoryByteSource` | `blendFile` | tests, fuzzing, decompressed data |
| `ArAssetByteSource` | `usdBlendFileFormat` | the importer, through `ArResolver` |

`ArAssetByteSource` lives in the bundle because it needs OpenUSD; `blendFile`
never sees `Ar`.

## 4. Compression

```text
ByteSource → CompressionReader → BlendContainerReader
```

Compression is detected from the first bytes, never from the file name:

| Magic | Format | Notes |
| --- | --- | --- |
| `BLENDER` | uncompressed | |
| `1F 8B` | gzip | older Blender versions |
| `28 B5 2F FD` | Zstandard | Blender 3.0 and later |

The first implementation decompresses into a `MemoryByteSource`, within the
limits of §2.4. Random access into Zstandard's seekable frames is a later
optimization (Phase 8); it is not assumed, because whether a given file is
written with useful frame boundaries is not under the reader's control.

The Phase 0 header probe is separate from full container decompression:
`ReadHeader` recognizes Zstandard magic and streams only the required 12 or
17 output bytes, including across concatenated frames. It reads at most 1 MiB
of compressed input in 4 KiB chunks and limits the decoder window to 8 MiB.
These fixed probe budgets do not resolve BLEND-O5's full-file limits. The
probe does not validate the remaining frames, payload or trailing checksums;
successful header validation is not validation of a complete `.blend`.

## 5. File header

### 5.1 Legacy header

Twelve bytes:

| Offset | Size | Content |
| --- | --- | --- |
| 0 | 7 | `BLENDER` |
| 7 | 1 | pointer size: `_` = 4 bytes, `-` = 8 bytes |
| 8 | 1 | endianness: `v` = little, `V` = big |
| 9 | 3 | file version, three ASCII digits, e.g. `405` for 4.5 |

### 5.2 Blender 5 header

Blender 5.0 changed the low-level file format: a longer header that states its
own size and a file-format version, and larger block headers (§6.2). The
reader identifies the header form from the bytes after `BLENDER` and selects
the container reader from it:

```cpp
enum class BlendContainerVersion {
    Legacy,
    Blender5,
};
```

The format-1 header is seventeen bytes:

| Offset | Size | Content |
| --- | --- | --- |
| 0 | 7 | `BLENDER` |
| 7 | 2 | header size as ASCII digits: `17` |
| 9 | 1 | `-` (8-byte pointers) |
| 10 | 2 | file-format version as ASCII digits: `01` |
| 12 | 1 | `v` (little endian) |
| 13 | 4 | file version as four ASCII digits, e.g. `0502` |

`Header` retains `containerVersion` and `headerSize` as well as pointer size,
byte order and file version. Unknown sizes and format versions are rejected,
not interpreted as legacy headers. Format 1 is not restricted to file
versions 5.x: Blender 4.5 can also read and write it.

The layout is confirmed against the
[Blender 5.0 release notes](https://developer.blender.org/docs/release_notes/5.0/core/#large-buffers-in-blend-files),
the [5.0 header definition](https://github.com/blender/blender/blob/v5.0.0/source/blender/blenloader_core/BLO_core_blend_header.hh),
and the contributor-provided `blender-5.2.2/Untitled.blend` corpus file.
The block layout portion of BLEND-O1 remains open.

## 6. Block layout

The file is a sequence of blocks after the header, ending with an `ENDB`
block. Each block is a block header followed by its data.

### 6.1 Legacy block header

| Field | Size | Meaning |
| --- | --- | --- |
| `code` | 4 | block code (§6.3) |
| `len` | 4 | data length in bytes, after the header |
| `old` | pointer size | the address the data had when saved; the pointer-map key |
| `SDNAnr` | 4 | index of the data's struct in SDNA |
| `nr` | 4 | number of structs in the data |

### 6.2 Blender 5 block header

Blender 5 block headers carry 64-bit lengths. The container reader normalizes
both layouts into one in-memory block record, so nothing above
`BlendContainerReader` knows which layout a file used:

```text
BlendContainerReader
        ├─ LegacyBlockReader
        └─ Blender5BlockReader
                ↓
         BlendBlock { code, length, oldAddress, sdnaIndex, count, offset }
```

### 6.3 Block codes

| Code | Content |
| --- | --- |
| two letters, e.g. `OB`, `ME`, `MA`, `IM`, `CA`, `LA`, `SC`, `GR`, `AR`, `AC`, `NT`, `LI` | an ID datablock: object, mesh, material, image, camera, light, scene, collection, armature, action, node tree, library |
| `DATA` | data owned by the preceding ID block |
| `GLOB` | file globals, including the current scene |
| `DNA1` | the SDNA |
| `REND`, `TEST`, `USER` | render info, thumbnail, preferences — ignored |
| `ENDB` | end of file |

An unknown code is skipped with an `unsupported-but-readable` diagnostic.

## 7. SDNA

The `DNA1` block holds:

| Section | Content |
| --- | --- |
| `SDNA` | identifier |
| `NAME` | count, then member names as NUL-terminated strings (with `*` and `[n]` decorations) |
| `TYPE` | count, then type names |
| `TLEN` | one 16-bit length per type |
| `STRC` | count, then per struct: type index, member count, and per member a type index and a name index |

Sections are 4-byte aligned. Decoding validates every count against the bytes
that remain and every index against its table, and computes each member's
offset from the file's own type lengths and pointer size. Member names are
parsed into a base name, a pointer level and array dimensions.

The SDNA is the only source of struct layout. Readers in `blendScene` ask for
members by name (`Object.parent`, `Mesh.totvert`, …), and a missing member is
a version difference handled by the decoder, not a crash.

## 8. ID graph reconstruction

- The **pointer map** maps every block's old address to the block.
- An **ID block** is followed by `DATA` blocks that hold its owned arrays and
  sub-structs.
- **References** between datablocks (object → mesh, object → parent, mesh →
  materials) are old pointers resolved through the map (§2.6).
- **`ListBase`** chains (`first`/`last`, `next`/`prev`) are walked through the
  map with a length limit and cycle detection.
- **Linked data** — IDs that live in another `.blend`, referenced through `LI`
  library blocks — is reported, not followed (BLEND-O3).

## 9. Version support

| Range | Design target |
| --- | --- |
| Blender 4.5 LTS | target for the first stable release |
| Blender 5.x | target for the first stable release |
| Blender 3.x – 4.4 | read where the decoders already cover it; not claimed |
| older than 3.0 | not targeted |

Mesh storage moved during 3.x and 4.x from fixed structs to generic attributes
and offset arrays. `blendScene` keeps one decoder per storage form, selected
from the SDNA and the file version, all producing the same Scene IR. The
supported range is restated, with a fixture behind each version, in
[reference/CAPABILITY_MATRIX.md](../reference/CAPABILITY_MATRIX.md) — never
claimed here. The minimum is BLEND-O2.

## 10. Source concepts

What each source concept becomes in the Scene IR. The Scene IR is
Blender-neutral: it holds converted values (USD basis, meters), identifiers,
and source names, never SDNA structs or old pointers.

| Source | Scene IR | Phase |
| --- | --- | --- |
| `GLOB` current scene | the scene to read | 0 |
| file version, SDNA | `SceneMetadata` provenance | 0 |
| `Scene` | `SceneMetadata`: frame range, fps, unit settings | 2 (5 for time) |
| scene collection hierarchy | the set of objects to author; `Collection` | 2 (4 for collections) |
| `Object` | `Object`: identifier, source name, type, parent, world matrix, visibility, data reference | 2 |
| `Mesh` | `Mesh`: points, face counts, corner indices, normals, UV sets, material indices | 2 |
| `Material`, its node tree | `Material`: the Principled BSDF subset ([MATERIAL_POLICY.md](MATERIAL_POLICY.md)) | 3 |
| `Image` | `Image`: source path, color space, packed flag | 3 |
| `Camera` | `Camera` | 4 |
| `Light` | `Light` | 4 |
| `Armature`, bones, vertex groups | `Skeleton`, skin weights | 6 |
| `Action`, F-Curves | `Animation`: sampled channels | 5 (objects), 6 (bones) |
| `Library` | a diagnostic (BLEND-O3) | 2 |

Objects with data the reader does not support (text, metaball, volume,
grease pencil, …) are authored as empties with an `unsupported-but-readable`
diagnostic, so the hierarchy stays intact.

## 11. What is not read

The native reader reads stored source data. It does not apply:

- the modifier stack;
- Geometry Nodes;
- constraints, drivers, or Python expressions;
- simulation and baked caches stored outside the file;
- Blender's do-versions logic beyond what a decoder needs to read a supported
  version's data.

A source object whose visible result depends on one of these is read as stored,
with a diagnostic. Evaluated data comes only from the Blender host backend
([BACKEND_POLICY.md §5](BACKEND_POLICY.md#5-source-and-evaluated-representations)).

## 12. Open questions

| Id | Question | Proposed answer | Blocks |
| --- | --- | --- | --- |
| BLEND-O1 | The exact byte layout of the Blender 5 block header; the file header is confirmed in §5.2. | Confirm against Blender 5.0's release notes and fixtures written by Blender 5.x; record the remaining layout in §6.2. | Phase 1 |
| BLEND-O2 | The minimum Blender version read. | 3.0, the first with Zstandard; claimed only from 4.5 LTS. | Phase 1 |
| BLEND-O3 | What happens to data linked from another `.blend`? | Reported in Phases 0–6. Later, possibly authored as a USD reference to the other `.blend`, which this file format then opens. | nothing (non-blocking) |
| BLEND-O4 | Are big-endian and 32-bit-pointer files supported? | Read by the legacy container reader where fixtures exist; not claimed. | Phase 1 |
| BLEND-O5 | The decompression size and ratio limits. | Set from a measured corpus of real files in Phase 1, overridable by the caller. | Phase 1 |

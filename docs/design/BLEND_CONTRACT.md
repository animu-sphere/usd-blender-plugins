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

Header probes are separate from full container decompression: `ReadHeader`
recognizes gzip and Zstandard magic and streams only the required 12 or 17
output bytes, including across concatenated gzip members or Zstandard frames.
Both read at most 1 MiB of compressed input in 4 KiB chunks. Zstandard's
decoder window is limited to 8 MiB; gzip uses DEFLATE's fixed 32 KiB window.
These fixed probe budgets do not resolve BLEND-O5's full-file limits. Probes
stop as soon as the required header bytes are available; they do not require
or validate the remaining payload or trailing checksums. A checksum needed
to advance past an intermediate member is still validated. Successful header
validation is not validation of a complete `.blend`.

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
The confirmed block layout is in §6.2.

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

The header is 20 bytes with 4-byte pointers and 24 bytes with 8-byte
pointers. The legacy reader decodes integers using the byte order and
pointer size declared by the file, including big-endian and 32-bit-pointer
layouts, regardless of the host. These paths require their own container and
SDNA fixtures before compatibility is claimed; synthetic header tests alone
establish only recognition of the declared layout. Format 1 has no such
variants (§5.2).

### 6.2 Blender 5 block header

Format 1 uses a 32-byte block header. Unlike the legacy layout, the SDNA
index precedes the old address, and both the data length and element count
are signed 64-bit integers:

| Offset | Field | Size | Meaning |
| --- | --- | --- | --- |
| 0 | `code` | 4 | block code (§6.3) |
| 4 | `SDNAnr` | 4 | signed index of the data's struct in SDNA |
| 8 | `old` | 8 | unsigned old address; the pointer-map key |
| 16 | `len` | 8 | signed data length in bytes, after the header |
| 24 | `nr` | 8 | signed number of structs in the data |

There is no padding between fields. Format 1 is little-endian with 8-byte
pointers (§5.2). A reader must reject negative lengths, counts and SDNA
indices before converting them to the normalized unsigned representation.
It must not decode this header as the 24-byte legacy 64-bit-pointer layout.
The field order and widths follow the
[Blender 5.0 block-header definition](https://github.com/blender/blender/blob/v5.0.0/source/blender/blenloader_core/BLO_core_bhead.hh).
The next block starts immediately after the declared payload, without
rounding to a pointer-alignment boundary. The repository-generated fixture
confirms this layout through the terminal `ENDB` header; the verification
evidence is recorded in [§12.1](#121-resolved-decisions).

The container reader normalizes both layouts into one in-memory block
record, so nothing above
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

Blender 3.0 is the native decoder's minimum design target (BLEND-O2), not a
minimum enforced by `ReadHeader`. Header recognition validates syntax and
reports the stored version and layout, even for older versions; it does not
promise container or scene compatibility. A successful probe must not be
used as evidence that scene decoding supports that version. The file's
container format, not its Blender version, selects the block layout: a 4.5
file can use format 1 (§5.2).

Mesh storage moved during 3.x and 4.x from fixed structs to generic attributes
and offset arrays. `blendScene` keeps one decoder per storage form, selected
from the SDNA and the file version, all producing the same Scene IR. The
supported range is restated, with a fixture behind each version, in
[reference/CAPABILITY_MATRIX.md](../reference/CAPABILITY_MATRIX.md) — never
claimed here.

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
| BLEND-O3 | What happens to data linked from another `.blend`? | Reported in Phases 0–6. Later, possibly authored as a USD reference to the other `.blend`, which this file format then opens. | nothing (non-blocking) |
| BLEND-O5 | The decompression size and ratio limits. | Set from a measured corpus of real files in Phase 1, overridable by the caller. | Phase 1 |

### 12.1 Resolved decisions

- **BLEND-O1 (2026-10-02):** format 1 uses the seventeen-byte file header
   in §5.2 and the 32-byte block header in §6.2. The Blender 5.0 release notes
   and tagged header definitions are the format references, not code adopted
   by this reader. An independent byte-level walk of the repository's
   [Blender 5.2.2 empty-scene fixture](../../plugins/usdBlendFileFormat/tests/fixtures/README.md#blender-written-empty-scene)
   verified 266 consecutive blocks: 262 `DATA`, one `GLOB`, one `SC`, one
   `DNA1` and one `ENDB`. Each payload fit the remaining bytes, and each
   length, count and SDNA index was nonnegative. `DNA1` at offset 53,922 had
   134,572 payload bytes beginning with `SDNA`; the zero-length, zero-count
   `ENDB` header at offset 188,526 ended exactly at byte 188,558. The fixture
   SHA256 matched its recorded provenance. This establishes the byte layout,
   not implemented container or SDNA support.

- **BLEND-O2 (2026-10-02):** retain 3.0 as the native decoder's minimum
   design target and 4.5 LTS/5.x as the first stable release targets (§9).
   Keep structural header recognition independent of scene-version policy.
   The `blendFile.header` regressions verify that valid legacy headers for
   2.99, 3.0 and 4.5 are recognized, and that format-1 headers for 4.5 are
   recognized both uncompressed and through the Zstandard probe. These are
   synthetic headers, not scene-compatibility evidence.

- **BLEND-O4 (2026-10-02):** the legacy container reader is designed to
   handle both pointer widths and byte orders using explicit byte decoding
   (§6.1); format 1 remains 64-bit little-endian only. Compatibility claims
   for legacy variants require container and SDNA fixtures. The
   `blendFile.header` regressions cover every legacy pointer-width/byte-order
   combination at versions 2.99, 3.0 and 4.5, but do not establish block or
   SDNA decoding.

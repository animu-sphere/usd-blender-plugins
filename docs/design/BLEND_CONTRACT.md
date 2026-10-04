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
   compressed bytes is limited, against decompression bombs (§4.2).
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
These fixed probe budgets are separate from §4.2's full-file policy. Probes
stop as soon as the required header bytes are available; they do not require
or validate the remaining payload or trailing checksums. A checksum needed
to advance past an intermediate member is still validated. Successful header
validation is not validation of a complete `.blend`.

### 4.1 Full-stream byte reading

`ReadFileBytes(ByteSource&, const CompressionLimits&)` returns an owning
`std::vector<std::byte>` of the uncompressed file bytes. A caller can wrap
those bytes in `MemoryByteSource` while keeping the result alive. Compression
is recognized by the magic above; uncompressed bytes are copied unchanged.
The decoded file header is validated, but blocks, `ENDB` and SDNA are not
validated by this API.

Every call supplies all four limits; zero-initialized limits are invalid:

| Field | Meaning |
| --- | --- |
| `maxInputBytes` | maximum source size, including all members, frames and metadata |
| `maxOutputBytes` | maximum decoded file size, also applied to uncompressed input |
| `maxExpansionRatio` | maximum integer ratio: decoded size may not exceed source size times this value; applies only to compressed input |
| `maxWindowLog` | base-2 logarithm of the maximum Zstandard window size, from 10 through 30; gzip always uses its fixed 32 KiB window |

Input size is checked before decoding. Output and ratio limits are checked
throughout decoding, with overflow-safe ratio arithmetic and bounded buffer
growth. A fixed scratch buffer permits detecting output one byte beyond a
limit without extending the returned buffer past that limit. Decoder and
output allocation failures become `BLEND_COMPRESSION_*` diagnostics.

Unlike the probe, full-stream reading requires every member or frame to end,
validates gzip CRC/ISIZE and any Zstandard checksum present, and rejects
truncation or trailing garbage. Concatenated members and frames share the
same whole-file limits. Zstandard skippable frames may occur after the
initial standard frame; skippable magic is not an initial format signature.
The entire decoded stream must begin with a valid `.blend` header, not
another compression envelope.

This API deliberately has no implicit defaults; callers supply every field.
The [committed-input measurements](../../plugins/usdBlendFileFormat/tests/corpus/README.md#compression-measurements)
pin exact byte, integer-ratio and decoder-window acceptance boundaries.
The accepted standard policy is in §4.2, with separate large-input evidence.
Existing unit-test budgets do not define that policy. Accepting it does not
change the importer's uncompressed-only boundary.

### 4.2 Standard full-stream limit policy

**Accepted 2026-10-05 (BLEND-O5).** A consumer choosing standard full-stream
budgets should supply this complete `CompressionLimits` value:

| Field | Standard value | Bound |
| --- | --- | --- |
| `maxInputBytes` | 268,435,456 | 256 MiB stored input |
| `maxOutputBytes` | 536,870,912 | 512 MiB decoded output |
| `maxExpansionRatio` | 4,096 | whole-file decoded/stored ratio |
| `maxWindowLog` | 23 | 8 MiB Zstandard decoder window |

These are bounded operating defaults, not a maximum supported asset size or
a claim that every legitimate `.blend` fits. All four fields remain
caller-overridable with §4.1's validation. Smaller application budgets are
valid; raising a budget is an explicit caller decision. A limit failure must
retain its `BLEND_COMPRESSION_*` diagnostic, never retry with a larger budget
or return partial data. All members, frames and skippable metadata share
the same whole-file budgets. The ratio check does not apply to uncompressed
input; both byte limits do.

The [dated measurement report](../reports/2026-10-05-compression-policy.md)
records Blender-written 4.5.13 and 5.2.2 large meshes, repetitive attributes
and packed images, plus generated gzip encodings. Across these inputs,
the largest stored compressed size was 67,323,617 bytes, largest decoded
size 94,565,524 bytes, maximum minimum integer ratio 1,344, and largest
minimum accepted window log 20. Stored and decoded budgets provide roughly
4x and 5.7x headroom, respectively; ratio 4,096 provides roughly 3x headroom
over the high-compressibility case. Window log 23 allows eight times the
largest minimum accepted window. Each choice is independently bounded;
ratio alone is not a memory budget.

This deliberately bounded generated corpus is not production-scene
certification, GiB-scale execution evidence, old Blender-written gzip
evidence, or a measured resident-memory cap. Decoder state, vector capacity,
SDNA, Scene IR and authoring can consume additional memory. Consumers with a
process-memory requirement must impose their own smaller budgets and
concurrency limits; streaming/range optimizations remain later work.

The policy is opt-in at the existing explicit-limit API and CLI boundaries.
Zero-initialized `CompressionLimits` stay invalid, the CLI still requires all
four options for compressed input, and the importer still rejects compressed
containers. Wiring automatic defaults or compressed scene importing requires
its own implementation and regressions; this decision does not imply either.

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

Legacy `len`, `SDNAnr` and `nr` are signed 32-bit fields. Negative values are
rejected before conversion, just as for format 1.

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

An unknown code's payload is skipped with an `unsupported-but-readable`
diagnostic; its record remains in the block enumeration.

### 6.4 Block enumeration boundary

`ReadBlocks(ByteSource&, uint64_t maxBlocks)` takes an uncompressed source
and returns `Result<std::vector<BlendBlock>>`. A compressed source must first
pass through `ReadFileBytes` (§4.1), then be wrapped in `MemoryByteSource`
while the decoded bytes remain alive. No production defaults are selected
by either API. The importer composes this boundary for uncompressed input
under its [structural budgets](DESIGN_POLICY.md#532-uncompressed-importer-boundary);
compressed scene imports remain outside that implemented boundary. The
accepted full-stream policy in §4.2 does not wire compression into it.

`BlendBlock` normalizes both layouts as follows:

| Field | Representation | Meaning |
| --- | --- | --- |
| `code` | four raw characters, including NUL padding | stored block code, not a host-endian integer |
| `length` | unsigned 64-bit | validated payload length |
| `oldAddress` | unsigned 64-bit | old pointer bits, zero-extended for 32-bit files |
| `sdnaIndex` | unsigned 32-bit | nonnegative stored SDNA index, not yet checked against SDNA |
| `count` | unsigned 64-bit | nonnegative element count, not yet checked against a struct length |
| `offset` | unsigned 64-bit | payload start in the uncompressed source; the block header precedes it |

`maxBlocks` is required, from 1 through the maximum unsigned 32-bit value,
and includes the terminal `ENDB` record. Header reads, declared payload
ranges and offset arithmetic are bounded by the source size. Enumeration
does not allocate or read payloads; it advances directly to each next header.
`ENDB` must have an empty payload and end exactly at the source size.
Missing or truncated headers, negative fields, oversized payloads, trailing
bytes, allocation failures and budget exhaustion return fatal diagnostics
with the uncompressed byte offset and block index. File-header failures keep
their `BLEND_HEADER_*` codes. Unknown codes return a recoverable
`BLEND_BLOCK_UNKNOWN_CODE` diagnostic with `Unsupported` severity.

This boundary validates framing only. It does not require or decode `DNA1`,
validate SDNA indices against a schema, check `count` against struct sizes,
build a pointer map, or claim Blender-version or scene compatibility.

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

### 7.1 Schema decoding boundary

`ReadDna(span<const byte> payload, const Header&)` receives exactly one
uncompressed `DNA1` payload and returns `Result<DnaSchema>`. The caller selects
the payload from `ReadBlocks` and supplies the file header; the pointer size
must be 4 or 8 and the byte order must be valid. Payload bytes may be released
after the call: the result owns its names, types, structures and members.
The span is the parser's byte boundary. Full-file input and output budgets
remain the responsibility of `ReadFileBytes` and its caller.

`DnaSchema` retains the `NAME` table, named `DnaType` records with their `TLEN`,
and `DnaStruct` records in stored order, preserving their SDNA indices.
`DnaMember` retains the type/name indices, parsed base name, pointer level,
array dimensions, byte offset and total byte size. `FindStruct(typeName)` and
`DnaStruct::FindMember(baseName)` return a pointer into the owning schema or
`nullptr` when absent; callers must not retain that pointer after mutating,
moving or destroying its owner.

Integer fields use the file byte order. Counts are bounded by remaining bytes
before allocation or iteration; string tables cannot exceed the 65,536 entries
addressable by 16-bit member indices. Strings must be nonempty and terminated.
Sections use four-byte payload-relative alignment, not host alignment.
Duplicate type names, structure types or member base names are rejected.

Member declarations use an ASCII identifier, optional leading pointer stars
and positive decimal array dimensions. Parenthesized function pointers such
as `(*callback)()` and `(*callbacks[2])()` are retained as pointer storage,
not executable functions. Other decorations are rejected rather than guessed.
Pointer elements use the file pointer size regardless of the pointee `TLEN`;
value elements use their type's `TLEN`. Array multiplication is checked for
overflow. Member offsets are cumulative, including explicitly stored padding
members; no host ABI padding is inserted. The total must equal the enclosing
type's `TLEN`. A zero-length structure with no members is valid, including the
`raw_data` sentinel in the Blender 5 corpus; zero-sized value members are not.

Malformed sections, counts, indices, names, sizes and trailing bytes fail with
fatal `BLEND_DNA_*` diagnostics. Their byte offsets are relative to the DNA1
payload; no block index is attached by this payload-only API. Allocation
failures also return a fatal diagnostic. This boundary does not select or
require exactly one DNA1 block in a file, validate other blocks' SDNA indices
or element counts, read member values, build a pointer map or decode a scene.

## 8. ID graph reconstruction

- The **pointer map** maps addressable ID and `DATA` blocks' nonzero old
   addresses to their block indices. Metadata blocks are not reference targets.
- An **ID block** is followed by `DATA` blocks that hold its owned arrays and
  sub-structs.
- **References** between datablocks (object → mesh, object → parent, mesh →
  materials) are old pointers resolved through the map (§2.6).
- **`ListBase`** chains (`first`/`last`, `next`/`prev`) are walked through the
  map with a length limit and cycle detection.
- **Linked data** — IDs that live in another `.blend`, referenced through `LI`
  library blocks — is reported, not followed (BLEND-O3).

### 8.1 Pointer-map boundary

`BuildPointerMap(span<const BlendBlock>)` returns an owning `PointerMap`.
ID codes are two uppercase ASCII letters followed by two NUL bytes. Only
these blocks and `DATA` enter the map; zero addresses are excluded. `GLOB`,
`REND`, `TEST`, `USER`, `DNA1`, `ENDB` and unknown non-ID codes are excluded.
The 5.2.2 corpus stores the same nonzero value, 16, in `REND` and `GLOB`;
metadata values must not be treated as unique saved object addresses.

Entries retain the original zero-based block indices, not pointers into the
caller's vector. The caller must interpret returned indices against the same
block sequence. Construction is bounded by that sequence, whose count budget
is imposed by `ReadBlocks`; more than the maximum unsigned 32-bit count fails.
Entries are sorted for logarithmic lookup. Duplicate nonzero addresses among
reference targets fail with `BLEND_POINTER_DUPLICATE`, attaching the later
block's payload offset and index. No first/last-wins aliasing is allowed.

`PointerMap::Resolve(uint64_t)` returns `Result<optional<uint32_t>>`. Zero
returns null without a diagnostic. An exact saved address returns its block
index; an absent nonzero address returns null with a recoverable
`BLEND_POINTER_UNRESOLVED` warning. Interior addresses are not inferred.
Lookup has no referring-block context, so that warning has no byte offset or
block index; a consumer may add context. Allocation failures and an excessive
block count produce fatal `BLEND_POINTER_ALLOCATION` and `BLEND_POINTER_LIMIT`
diagnostics. This API neither reads pointer-valued members nor traverses graphs.

Blender-written 5.2.2 multiple-Mesh files can reuse saved addresses for
different `Attribute` and `AttributeArray` payloads. The reader's global map
still rejects them. Scene selection and decoding use a separate,
SDNA-aware [Mesh ownership contract](DESIGN_POLICY.md#528-native-mesh-storage-boundary);
it is not a relaxation of this API's uniqueness requirement.

### 8.2 Raw datablock boundary

`ListDatablocks(span<const byte> bytes, span<const BlendBlock> blocks,
const DnaSchema&)` consumes the same uncompressed bytes and ordered records
used for `ReadBlocks`, plus their decoded `ReadDna` schema. It returns owning
`RawDatablock` records in file order: `blockIndex`, `oldAddress`, `typeName`
and `name`. Output contains every two-letter ID block, not `DATA` or metadata.
The caller retains full-file and block budgets; no production defaults are
introduced. This does not select or verify the file's DNA1 block count.

Each ID payload must fit the supplied bytes, have a valid SDNA struct/type
index, and contain exactly one structure with length equal to its `TLEN`.
The structure is either `ID` itself or contains a non-pointer, non-array `id`
member of type `ID`. The embedded member and its `name` must fit their enclosing
structures. `ID.name` must be a non-pointer, one-dimensional `char` array,
contain its two-byte prefix, and have a NUL terminator within that array.

`name` preserves the bytes before the first NUL, including the stored prefix;
UTF-8 validation, normalization and prefix removal belong to consumers. The
prefix need not match the block code: both corpus files have `SN` screen
blocks with `bScreen` SDNA type and `SR` name prefixes. Layout comes only from
SDNA, not host structs or hard-coded version offsets. Output remains valid
after the input bytes, blocks or schema are released.

Invalid ID ranges use `BLEND_BLOCK_SIZE`; invalid indices, missing or wrongly
typed members, size/count mismatches and invalid names use `BLEND_DNA_INDEX`,
`BLEND_DNA_MEMBER`, `BLEND_DNA_SIZE` and `BLEND_DNA_NAME`. These fatal diagnostics
attach the ID payload's uncompressed file offset and block index, unlike the
payload-relative `ReadDna` API. Allocation failures use `BLEND_DNA_ALLOCATION`;
an excessive record count uses `BLEND_BLOCK_COUNT_LIMIT`. Non-ID payloads are
not schema-validated by this API, and it reads no member values other than
ID names. Linked-file references, lists, scene objects and USD are outside
this boundary.

### 8.3 Borrowed SDNA value boundary

`ViewDnaBlock(bytes, blocks, schema, header, blockIndex, elementIndex = 0)`
binds one structure element of a caller-selected uncompressed block. It
returns a `DnaValueView`, not an owning scene record. The caller supplies the
same decoded bytes, ordered blocks, `ReadDna` schema and layout header. Bytes
and schema, including their strings and member vectors, must remain alive
and unmodified for every derived view; blocks and header are not borrowed.
Moving or releasing the backing bytes/schema invalidates the views. Binding
does not find DNA1 or choose semantic blocks for the caller.

Binding validates the header layout, block/structure/type indices, payload
range, nonzero TLEN, exact `length / TLEN == count` and the selected element
index without overflowing a count multiplication. Zero-length structures
remain legal in the schema but cannot be bound as block elements. No view
reads beyond its validated span or uses host struct layout/alignment.

`Type`, `Bytes`, `PointerLevel` and `ArrayDimensions` expose borrowed source
facts. `Member(baseName)` selects a member of a scalar, non-pointer embedded
structure and checks its declared size, dimensions and range. `Element(index)`
consumes one leading array dimension; multidimensional arrays require repeated
selection. Neither method follows saved pointers, traverses lists or recurses
automatically. A `Pointer` read requires a scalar pointer value, preserves null
and all 32/64-bit address bits, and uses the stored byte order. Multiple and
function pointers expose storage bits only, never dereference or execution.

Scalar readers require non-pointer, non-array values and explicit type/width
matches:

| Reader | Accepted SDNA types and TLEN |
| --- | --- |
| `SignedInteger` | `int8_t` / `signed char`: 1; `short`: 2; `int`: 4; `long`: 4 or 8; `int64_t`: 8 |
| `UnsignedInteger` | `uint8_t` / `uchar` / `unsigned char`: 1; `ushort`: 2; `uint`: 4; `ulong`: 4 or 8; `uint64_t`: 8 |
| `FloatingPoint` | IEEE `float`: 4; IEEE `double`: 8; returned as `double` |

Signed reads sign-extend; unsigned reads retain all bits. Plain `char`, `bool`,
unknown names and mismatched widths are not inferred as numeric types; use
bounded `Bytes` for raw character storage. Floating-point reads retain
nonfinite numeric values, leaving semantic validation to consumers. No basis,
unit, string encoding or identifier conversion happens here.

Failures are fatal and non-recoverable: `BLEND_BLOCK_SIZE`, `BLEND_DNA_LAYOUT`,
`BLEND_DNA_INDEX`, `BLEND_DNA_MEMBER`, `BLEND_DNA_SIZE` and `BLEND_DNA_VALUE`.
They attach the selected block index and the current view's uncompressed file
offset; binding failures use the block payload offset (zero if the block index
is absent). No automatic unresolved-reference policy is added. The caller
passes saved address values to `PointerMap::Resolve` separately.

Fixture evidence is owned by the
[capability matrix](../reference/CAPABILITY_MATRIX.md#1-container), including
normal saves' `FileGlobal.curscene` references and the Scene-only library's
stored null reference. This boundary does not define a Scene-selection fallback
for libraries or publish a Scene IR.

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

### 12.1 Resolved decisions

- **BLEND-O5 (2026-10-05):** accept the caller-overridable standard
   full-stream budgets in §4.2, based on the linked large-mesh,
   high-compressibility and packed-asset measurements. Existing explicit-limit
   APIs, CLI argument requirements, header probes and uncompressed importer
   behavior remain unchanged. This resolves the policy question, not automatic
   default selection or compressed scene importing.

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

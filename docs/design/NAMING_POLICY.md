# Naming policy

> Status: **proposed**, 2026-10-01. This document defines intended behavior;
> implemented behavior belongs to the
> [capability matrix](../reference/CAPABILITY_MATRIX.md). Phase scope belongs
> to [DESIGN_POLICY.md §14](DESIGN_POLICY.md#14-phases).
>
> This document owns source names, USD identifiers, collisions and asset
> paths. On this area it wins over the design policy.

## 1. Scope

Blender datablock names are user text. They are not valid USD identifiers in
general (`Cube.001`, `A/B`, `日本語オブジェクト`), and they are not unique
across the places USD needs uniqueness. This document turns them into
identifiers that are valid, unique among siblings, and **the same every time
the same `.blend` is opened** — which reference, override and cache stability
depend on.

## 2. Source names

- Blender ID names are UTF-8. The two-character type prefix Blender stores in
  front of an ID name (`OB`, `ME`, …) is removed.
- A name is unique per ID type within one `.blend`, but an object and a mesh
  can share a name, and sanitizing can make two names equal.
- Bone, UV map, color attribute and vertex group names are UTF-8 strings
  unique within their owner.
- Invalid UTF-8 in a name is replaced with U+FFFD for display, and reported
  with `BLEND_NAME_INVALID_UTF8`.

## 3. Identity versus display

Every authored prim has two names:

| Name | Where | Rule |
| --- | --- | --- |
| identifier | the prim name | ASCII, valid USD identifier, deterministic (below) |
| source name | `customData["blend:sourceName"]`, and `displayName` when it differs from the identifier | the Blender name, exact |

The identifier is derived from the source name:

1. Every character outside `[A-Za-z0-9_]` becomes `_`.
2. Runs of `_` produced by step 1 collapse to one; a trailing produced `_` is
   removed.
3. If the result starts with a digit, `_` is prepended.
4. If nothing remains, the identifier is the kind fallback: `Object`,
   `Material`, `Camera`, `Light`, `Armature`, `Collection`, `UVMap`.

Literal source underscores are retained, including repeated and trailing ones.
Only runs and trailing underscores introduced by replacing other characters
are collapsed or removed; for example, `A/__B` becomes `A___B`.

Identifiers are ASCII so that every OpenUSD release, file system and USDZ
consumer accepts them, as in the sibling repositories. Whether to use OpenUSD's
UTF-8 identifiers instead is NAME-O1.

Fixed child names — `mesh`, `curves`, `points`, `preview`, the scopes of
[STAGE_CONTRACT.md §4](STAGE_CONTRACT.md#4-prim-hierarchy) — are not derived
from source names and always win over a derived one.

## 4. Collisions and order

Within one parent, candidates are ordered by their **source name's UTF-8
bytes**, and identifiers are assigned in that order. The first candidate keeps
its identifier; a later one that collides gets the smallest suffix `_1`, `_2`,
… that is free. A fixed child name (§3) is reserved before any derived name is
assigned.

Ordering by source name, not by position in the file, keeps identifiers stable
when Blender re-saves a file and reorders its blocks. Blender names are unique
per type, so the order is total within a scope.

Authored children appear in the same order.

### 4.1 Native Object naming boundary

`ObjectIdentifiers(scene)` in `blendScene/Naming.h` returns owning identifiers
aligned with the input Object vector. It applies the ASCII rules above to the
current Mesh/Empty scope, with `Object` as the fallback for either kind.
Each IR parent index defines a sibling scope; absent parents share the geometry
root scope. A Mesh parent reserves `mesh` before naming its Object children.
An Empty parent has no data child to reserve, and a root Mesh does not reserve
`mesh` among root Objects. Natural suffixes participate in the same occupied-name
set: a source `A_B_1` can collide with an earlier assigned suffix.

`SourceNameLess` compares names as unsigned UTF-8 bytes, not with a locale or
an input-order tie-break. Duplicate sibling source names fail with `BLEND_NAME_DUPLICATE`,
because that scope has no total source-name order. Invalid immediate parent
or Mesh indices fail with `BLEND_SCENE_REFERENCE_INVALID`. The helper expects
an otherwise validated Scene IR; it neither traverses nor validates parent
chains. Allocation failure uses `BLEND_NAME_ALLOCATION`.

`DecodeScene` calls this pass before publishing the Scene and copies its results
into `Object.identifier`. It retains raw `sourceName` bytes, Object discovery
order, parent and shared Mesh indices, visibility and normalized geometry.
It does not reorder the IR: USD authoring must visit sibling Objects in
the source-byte order above, not in IR vector or identifier order.

`NameForDisplay(sourceName)` provides separate owning UTF-8 display text.
Valid scalar sequences are retained exactly. Each maximal ill-formed subpart
is replaced with U+FFFD; overlong encodings, surrogate code points, out-of-range
scalars and truncated sequences are invalid. One recoverable
`BLEND_NAME_INVALID_UTF8` warning per affected name retains the raw name as
diagnostic context. `ObjectIdentifiers` propagates these warnings; native
decoding adds each selected Object's payload offset and block index. Display
repair never changes identifier allocation or raw source provenance.

This boundary does not author `blend:sourceName` or `displayName`, assign
Material/Collection/UV identifiers, or resolve asset paths. It implements the
proposed ASCII policy without closing NAME-O1's final decision. Fixture-backed
evidence belongs in the
[capability matrix](../reference/CAPABILITY_MATRIX.md#5-scene-ir).

### 4.2 UV map naming boundary

`UvIdentifiers(mesh)` uses the same sanitization, source-byte ordering,
collision allocator and display diagnostics as Object naming. Results align
with the input UV vector; the helper does not reorder it. `UVMap` is the
fallback for names with no identifier characters. UV source names must be
unique within their Mesh, including the active map.

`st` is reserved before assignment. The one active-render map, if any,
receives `st` regardless of its source name; other maps use their sanitized
names with the smallest free suffix. A non-render map named `st` therefore
becomes `st_1`, even if no map is active, so no render selection is invented.
Multiple active maps fail with `BLEND_NAME_RENDER_UV_INVALID`; duplicate source
names fail with `BLEND_NAME_DUPLICATE`. No aliases for the render map are made.

The bundle's [Scene IR authoring boundary](DESIGN_POLICY.md#531-scene-ir-usd-authoring-boundary)
visits Objects and UV properties in the same unsigned source-byte order.
Object/Mesh prims and UV properties carry exact raw
`customData["blend:sourceName"]`, with repaired UTF-8 `displayName` when it
differs from the identifier. The native decoder continues to retain UV source
names and active flags, without storing or applying USD primvar identifiers.
This does not close NAME-O1 or introduce Material/Collection naming.

## 5. Examples

| Source name | Identifier |
| --- | --- |
| `Cube` | `Cube` |
| `Cube.001` | `Cube_001` |
| `A/B` | `A_B` |
| `3D Text` | `_3D_Text` |
| `日本語オブジェクト` | `Object` |
| `日本語オブジェクト2` | `_2` |
| an object named `mesh` with a parent | `mesh_1` (the data child keeps `mesh`) |
| `Cube.001` and `Cube_001` under one parent | `Cube_001` (for `Cube.001`, the smaller bytes) and `Cube_001_1` |

The full source name always survives in `blend:sourceName` and `displayName`.

## 6. Asset paths

- A Blender path starting with `//` is relative to the `.blend`; it is written
  as `./` followed by the rest.
- Separators are written as `/`; Windows drive letters and UNC prefixes are
  kept as stored, and reported (`BLEND_IMAGE_ABSOLUTE_PATH`).
- Paths are not resolved, probed or rewritten at read time; the asset resolver
  resolves them ([DESIGN_POLICY.md §9](DESIGN_POLICY.md#9-asset-resolution-boundary)).
- Non-ASCII file names are kept as UTF-8.

## 7. Open questions

| Id | Question | Proposed answer | Blocks |
| --- | --- | --- | --- |
| NAME-O1 | ASCII identifiers, or OpenUSD's UTF-8 identifiers? | ASCII, as the siblings, until every consumer in the ecosystem is on a release that accepts UTF-8 identifiers. | Phase 2 |
| NAME-O2 | Is the kind fallback enough for scenes with many non-Latin names? | Measure on real files; if suffixes dominate, add a deterministic transliteration or a short hash of the source name. | nothing (non-blocking) |

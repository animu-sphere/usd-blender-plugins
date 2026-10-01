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
   `Material`, `Camera`, `Light`, `Armature`, `Collection`.

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

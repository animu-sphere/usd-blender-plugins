# Current milestones

Upcoming milestones, broken into incomplete tasks. A task leaves this page
when it lands; a Phase leaves it when every task has. Scope is
[DESIGN_POLICY.md §14](../design/DESIGN_POLICY.md#14-phases); the release a
Phase lands in and its status are in the
[status table](README.md#status-at-a-glance).

Current compatibility work is limited to Blender 5.x under the
[version policy](../design/BLEND_CONTRACT.md#9-version-support).
Existing older-version decoders, fixtures and regression tests remain, but
extending their coverage does not block the current milestones. Full
older-version compatibility is deferred to Phase 8 below.

## Phase 2 — objects and meshes

Goal: a `.blend` containing a cube opens directly in `usdview` as a
`UsdGeomMesh` at `/Asset/geo/Cube/mesh`, with deterministic hierarchy,
transforms and mesh data under the
[stage contract](../design/STAGE_CONTRACT.md).

| # | Task | Done when | Status |
| --- | --- | --- | --- |
| 2.3 | [Native scene decoding and ID graph](../design/DESIGN_POLICY.md#527-native-scene-decoding-boundary) | complete the mesh/empty milestone scope beyond the current mesh-storage boundary, composing selection and [saved Object values](../design/DESIGN_POLICY.md#526-saved-object-value-boundary); recursive instance references retain bounded missing, linked, invalid and cycle diagnostics | 🚧 |
| 2.5 | [Blender 5.x mesh decoding](../design/DESIGN_POLICY.md#528-native-mesh-storage-boundary) | complete the 5.x mesh scope beyond the fixture-backed CustomData/AttributeArray/AttributeSingle, packed custom-normal and [Mesh-domain oracle](../../tests/fixtures/native-mesh/README.md) boundary; points, topology, normals and indexed UVs populate the IR with tested empty/invalid/unsupported diagnostics; older-version storage and normal modes are not a completion requirement | 🚧 |

The [Scene IR boundary](../design/DESIGN_POLICY.md#521-scene-ir-foundation)
keeps basis conversion separate from binary decoding and USD authoring.
Task 2.5's additional
[5.2 polygon-fan evidence](../../tests/fixtures/native-normals/README.md#blender-52-polygon-fans-and-corner-angles)
pins modern corner-angle weights on concave/nonplanar polygons and point/split
fans through native decoding and registered-plugin reads. This is another
bounded fixture-backed Mesh boundary, not completion of general 5.x coverage.
Task 2.3's native boundary now has an
[integrated Blender-written Scene oracle](../../tests/fixtures/native-scene/README.md)
combining Mesh/Empty hierarchy, shared Meshes, a parent-only Mesh outside
membership, visibility, identifiers, transforms and geometry. Registered-plugin
tests now compare this composed stage against the saved oracle.
Task 2.3 also has a bounded
[5.2.2 unsupported-data fallback oracle](../../tests/fixtures/native-scene/README.md#unsupported-data-fallback-oracle):
known Camera/Light/text/Image Empty data retains diagnostic-bearing Empty
Objects, including unsupported parents of supported Meshes and a parent-only
Camera outside membership. Native and registered-plugin tests preserve
hierarchy, source transforms, own visibility, metadata/repeats/references and
exact warning context. Unknown types, linked/invalid references, active
Collection instances and unsupported transforms remain fatal; this does not
complete general Scene coverage or introduce Camera/Light data schemas.
Separate [multi-scale fixtures](../../tests/fixtures/native-units/README.md)
establish the unit and ASCII identifier policies through native-to-USD
composition and registered-plugin reads. Tasks 2.4 and 2.7 have landed for
the supported Mesh/Empty boundary. The importer composes native
decoding and authoring for uncompressed, gzip and Zstandard inputs. Uncompressed
byte budgets use the stored size; block budgets use decoded size and graph budgets
use the enumerated block count. `metadataOnly`
retains Object transforms and typed Mesh children without geometry attributes;
lazy decoding remains Phase 8 scope.
Compressed importing explicitly selects the accepted
[full-stream limit policy](../design/BLEND_CONTRACT.md#42-standard-full-stream-limit-policy).
Blender-written 5.2.2 compressed corpus and generated gzip/Zstandard encodings
of the existing Cube, integrated Scene and fallback oracles cover bounded
importer composition, limits, corruption, metadata, repeats and references.
This does not complete general 5.x Scene/Mesh coverage or add old Blender-written
gzip evidence.

## Phase 8 — performance and robustness

After the Blender 5.x path, revisit full older-version compatibility alongside
the phase's robustness work. Select a bounded version range before adding
fixtures; existing regression evidence alone does not establish full support.

| # | Task | Done when | Status |
| --- | --- | --- | --- |
| 8.1 | [Older-Blender compatibility](../design/BLEND_CONTRACT.md#9-version-support) | choose and document the older-version range and feature scope; add Blender-written oracle evidence for the chosen storage and normal modes, including older packed custom normals where in scope; verify native IR and registered-plugin behavior while preserving 5.x regressions; expand the capability matrix only for fixture-proven scope | ⬜ |

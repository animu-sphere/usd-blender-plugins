# Current milestones

Upcoming milestones, broken into incomplete tasks. A task leaves this page
when it lands; a Phase leaves it when every task has. Scope is
[DESIGN_POLICY.md §14](../design/DESIGN_POLICY.md#14-phases); the release a
Phase lands in and its status are in the
[status table](README.md#status-at-a-glance).

## Phase 2 — objects and meshes

Goal: a `.blend` containing a cube opens directly in `usdview` as a
`UsdGeomMesh` at `/Asset/geo/Cube/mesh`, with deterministic hierarchy,
transforms and mesh data under the
[stage contract](../design/STAGE_CONTRACT.md).

| # | Task | Done when | Status |
| --- | --- | --- | --- |
| 2.3 | [Native scene decoding and ID graph](../design/DESIGN_POLICY.md#527-native-scene-decoding-boundary) | complete the mesh/empty milestone scope beyond the current mesh-storage boundary, composing selection and [saved Object values](../design/DESIGN_POLICY.md#526-saved-object-value-boundary); recursive instance references retain bounded missing, linked, invalid and cycle diagnostics | 🚧 |
| 2.5 | [Version-aware mesh decoding](../design/DESIGN_POLICY.md#528-native-mesh-storage-boundary) | complete mesh storage beyond the fixture-backed CustomData/AttributeArray/AttributeSingle, packed custom-normal, [Mesh-domain oracle](../../tests/fixtures/native-mesh/README.md) and 3.3 MLoopUV/fixed-array default/auto-smooth normal boundary; points, topology, normals and indexed UVs populate the IR with tested empty/invalid/unsupported diagnostics | 🚧 |

The [Scene IR boundary](../design/DESIGN_POLICY.md#521-scene-ir-foundation)
keeps basis conversion separate from binary decoding and USD authoring.
Task 2.3's native boundary now has an
[integrated Blender-written Scene oracle](../../tests/fixtures/native-scene/README.md)
combining Mesh/Empty hierarchy, shared Meshes, a parent-only Mesh outside
membership, visibility, identifiers, transforms and geometry. Registered-plugin
tests now compare this composed stage against the saved oracle.
Separate [multi-scale fixtures](../../tests/fixtures/native-units/README.md)
establish the unit and ASCII identifier policies through native-to-USD
composition and registered-plugin reads. Tasks 2.4 and 2.7 have landed for
the supported uncompressed Mesh/Empty boundary. The importer composes native
decoding and authoring; byte budgets use the stored size and graph budgets use
the enumerated block count, not arbitrary production constants. `metadataOnly`
retains Object transforms and typed Mesh children without geometry attributes;
lazy decoding remains Phase 8 scope.
Legacy MVert/MLoop/MPoly geometry has four-layout synthetic coverage,
including signed/unsigned indices, absent modern members, CustomData aliases,
normal flags and UV composition, without repairing modern storage.
The unchanged
[Blender 3.3.21 fixture](../../tests/fixtures/native-mesh/README.md#legacy-scene-decoding)
now compares full owning IR and registered-plugin stages to its saved oracle,
including default legacy point normals, transforms, shared geometry, empty
domains and exact indexed MLoopUVs. Unverified versions and legacy normal
modes retain contextual fatal diagnostics. Four saved
[3.3.21 auto-smooth fixtures](../../tests/fixtures/native-normals/README.md#legacy-auto-smooth)
now pin angle thresholds, sharp/flat boundaries and connected fan normals
through owning IR, USD authoring and registered-plugin oracle comparisons.
Packed custom normals in older storage and other 3.x–4.4 version evidence
remain incomplete; the existing modern decoding boundary is unchanged.
The accepted [full-stream limit policy](../design/BLEND_CONTRACT.md#42-standard-full-stream-limit-policy)
does not broaden the importer's uncompressed-only boundary; compressed scene
importing still requires separate implementation and evidence.

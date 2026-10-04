# Current milestones

Upcoming milestones, broken into incomplete tasks. A task leaves this page
when it lands; a Phase leaves it when every task has. Scope is
[DESIGN_POLICY.md §14](../design/DESIGN_POLICY.md#14-phases); the release a
Phase lands in and its status are in the
[status table](README.md#status-at-a-glance).

## Phase 1 — container and SDNA

Goal: `.blend` binary structure is read by independent C++, stably, and can be
looked at.

| # | Task | Done when | Status |
| --- | --- | --- | --- |
| 1.1 | Resolve BLEND-O5 | the owning document records the answer | 🚧 |

## Phase 2 — objects and meshes

Goal: a `.blend` containing a cube opens directly in `usdview` as a
`UsdGeomMesh` at `/Asset/geo/Cube/mesh`, with deterministic hierarchy,
transforms and mesh data under the
[stage contract](../design/STAGE_CONTRACT.md).

| # | Task | Done when | Status |
| --- | --- | --- | --- |
| 2.3 | [Native scene decoding and ID graph](../design/DESIGN_POLICY.md#527-native-scene-decoding-boundary) | complete the mesh/empty milestone scope beyond the current mesh-storage boundary, composing selection and [saved Object values](../design/DESIGN_POLICY.md#526-saved-object-value-boundary); recursive instance references retain bounded missing, linked, invalid and cycle diagnostics | 🚧 |
| 2.4 | Objects, parenting and transforms | connect the fixture-backed native and USD local-transform boundaries to the importer, retaining Mesh/Empty render visibility and hierarchy, including nonuniform and negative scales, with explicit singular-parent failures | 🚧 |
| 2.5 | [Version-aware mesh decoding](../design/DESIGN_POLICY.md#528-native-mesh-storage-boundary) | complete mesh storage beyond the fixture-backed CustomData/AttributeArray/AttributeSingle, packed custom-normal and synthetic MLoopUV boundary; points, topology, normals and indexed UVs populate the IR with tested empty/invalid/unsupported diagnostics | 🚧 |
| 2.7 | USD authoring from the Scene IR | the importer connects the [fixture-backed authoring boundary](../design/DESIGN_POLICY.md#531-scene-ir-usd-authoring-boundary) to object Xforms and Mesh children under `/Asset/geo`, with local matrices, extent, right-handed polygon topology, normals and UVs; shared meshes are duplicated per object and no second basis or unit conversion occurs | 🚧 |
| 2.8 | Cube milestone and regression fixtures | a reproducible `single_cube.blend` opens at `/Asset/geo/Cube/mesh`; parenting, transforms, normals and UV fixtures match an oracle, repeated reads author the same stage, and both build modes and dependency gates pass | ⬜ |

The [Scene IR boundary](../design/DESIGN_POLICY.md#521-scene-ir-foundation)
keeps basis conversion separate from binary decoding and USD authoring.
Task 2.3's native boundary now has an
[integrated Blender-written Scene oracle](../../tests/fixtures/native-scene/README.md)
combining Mesh/Empty hierarchy, shared Meshes, a parent-only Mesh outside
membership, visibility, identifiers, transforms and geometry. This establishes
the fixed-scale library composition, not milestone importer/backend wiring.
Separate [multi-scale fixtures](../../tests/fixtures/native-units/README.md)
establish the unit and ASCII identifier policies through native-to-USD
composition; the importer/backend connection remains upcoming work.
Task 2.7 now has a separately tested Scene IR-to-layer boundary, including
Blender-written transform oracles and independent two-Mesh fixtures, but remains
incomplete until the importer connects it to native decoding. The input path
still reads headers only; no production compression or traversal defaults
were introduced.
Phase 1's open compression decision remains open; compressed importer inputs
must not acquire arbitrary production defaults to reach this milestone.

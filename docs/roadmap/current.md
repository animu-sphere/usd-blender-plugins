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
| 2.2 | Resolve STAGE-O1 and NAME-O1 | the selected meter-normalization policy is proven by Blender-written multi-scale cube/parenting fixtures through native decoding and USD authoring; the naming contract records its fixture-backed identifier policy | ⛔ |
| 2.3 | [Native scene decoding and ID graph](../design/DESIGN_POLICY.md#522-saved-scene-selection-boundary) | compose selection and [saved Object values](../design/DESIGN_POLICY.md#526-saved-object-value-boundary) into native Scene IR decoding; recursive instance references have bounded missing, linked, invalid and cycle diagnostics | 🚧 |
| 2.4 | Objects, parenting and transforms | mesh and empty objects retain render visibility and hierarchy; converted world matrices produce local transforms matching a Blender oracle, including nonuniform and negative scales | ⬜ |
| 2.5 | Version-aware mesh decoding | 4.5 and 5.x source positions, topology, corner normals and indexed UV maps populate the IR; empty meshes, invalid indices and unsupported storage have tested diagnostics | ⬜ |
| 2.6 | Deterministic identifiers | names and sibling collisions follow the naming contract, preserve source names and remain stable across repeated reads and input enumeration order | ⬜ |
| 2.7 | USD authoring from the Scene IR | the importer authors object Xforms and Mesh children under `/Asset/geo`, with local matrices, extent, right-handed polygon topology, normals and UVs; shared meshes are duplicated per object and no second basis or unit conversion occurs | ⬜ |
| 2.8 | Cube milestone and regression fixtures | a reproducible `single_cube.blend` opens at `/Asset/geo/Cube/mesh`; parenting, transforms, normals and UV fixtures match an oracle, repeated reads author the same stage, and both build modes and dependency gates pass | ⬜ |

Task 2.2 awaits the Blender-written multi-scale cube/parenting fixtures required
by the [unit policy](../design/STAGE_CONTRACT.md#61-scene-units); independent
Phase 2 decoding and graph-validation work continues.

The [Scene IR boundary](../design/DESIGN_POLICY.md#521-scene-ir-foundation)
keeps basis conversion separate from binary decoding and USD authoring.
Phase 1's open compression decision remains open; compressed importer inputs
must not acquire arbitrary production defaults to reach this milestone.

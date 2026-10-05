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

## Phase 3 — materials and images

| # | Task | Done when | Status |
| --- | --- | --- | --- |
| 3.1 | [External image and normal-map subset](../design/MATERIAL_POLICY.md#5-supported-node-subset) | decode supported Image Texture/UV Map/Normal Map paths, author standard texture/primvar-reader shaders inside `preview`, verify relative/absolute paths and unsupported image diagnostics with Blender 5.x oracles and registered-plugin tests | ⬜ |
| 3.2 | [Alpha and Emission mapping](../design/MATERIAL_POLICY.md#9-open-questions) | resolve MAT-O1/MAT-O4 for the maintained version scope using fixtures/render evidence, replace deferred-input fallback with the agreed opacity/threshold/emissive inputs, preserve constants and binding regressions | ⬜ |
| 3.3 | [Color attributes](../design/STAGE_CONTRACT.md#18-open-questions) | resolve STAGE-O3, choose and fixture-test color domains/types and active-render `displayColor` mapping, and update the capability matrix only for proven scope | ⬜ |
| 3.4 | [Generic-renderer acceptance](../design/DESIGN_POLICY.md#14-phases) | verify base color, textures, alpha and normal maps in a generic renderer, every binding under `/Asset/mtl`, both build modes and hosted Windows/Linux gates | ⬜ |

## Phase 8 — performance and robustness

After the Blender 5.x path, revisit full older-version compatibility alongside
the phase's robustness work. Select a bounded version range before adding
fixtures; existing regression evidence alone does not establish full support.

| # | Task | Done when | Status |
| --- | --- | --- | --- |
| 8.1 | [Older-Blender compatibility](../design/BLEND_CONTRACT.md#9-version-support) | choose and document the older-version range and feature scope; add Blender-written oracle evidence for the chosen storage and normal modes, including older packed custom normals where in scope; verify native IR and registered-plugin behavior while preserving 5.x regressions; expand the capability matrix only for fixture-proven scope | ⬜ |

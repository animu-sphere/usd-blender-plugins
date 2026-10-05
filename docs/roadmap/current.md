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

## Phase 8 — performance and robustness

After the Blender 5.x path, revisit full older-version compatibility alongside
the phase's robustness work. Select a bounded version range before adding
fixtures; existing regression evidence alone does not establish full support.

| # | Task | Done when | Status |
| --- | --- | --- | --- |
| 8.1 | [Older-Blender compatibility](../design/BLEND_CONTRACT.md#9-version-support) | choose and document the older-version range and feature scope; add Blender-written oracle evidence for the chosen storage and normal modes, including older packed custom normals where in scope; verify native IR and registered-plugin behavior while preserving 5.x regressions; expand the capability matrix only for fixture-proven scope | ⬜ |

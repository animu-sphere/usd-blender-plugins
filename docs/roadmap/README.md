# Roadmap

The task detail holds only **incomplete** work; the phase status table also
records completed phases. When a Phase lands, its task detail leaves this
directory: shipped scope goes to the
[changelog](../../CHANGELOG.md) and, once versions are tagged, to per-version
release records; the implemented state goes to
[architecture/](../architecture/) and [reference/](../reference/). The roadmap
is not a second changelog. Design rationale lives in [design/](../design/).

Legend: ✅ done · 🚧 in progress · ⬜ not started · ⛔ blocked

| Document | Contents |
| --- | --- |
| [current.md](current.md) | Upcoming milestones, broken into incomplete tasks with completion criteria. |

## One sequence

This repository has one phase sequence, `Phase 0`–`Phase 8`, defined in
[DESIGN_POLICY.md §14](../design/DESIGN_POLICY.md#14-phases). A phase is a
unit of scope; a release is a scheduling decision. The mapping between them is
made **only** in the table below.

## Status at a glance

**This table is the single source of truth for phase status and which release
a Phase lands in.** Other documents link here instead of repeating either.
Completion does not imply that the planned release has been tagged.

| Phase | Scope | Status | Release |
| --- | --- | --- | --- |
| 0 | workspace skeleton, minimal file format | ✅ | [v0.1.0](../releases/v0.1.0.md) |
| 1 | container and SDNA, `blend_inspect` | ✅ | [v0.1.0](../releases/v0.1.0.md) |
| 2 | objects and meshes | ✅ | [v0.1.0](../releases/v0.1.0.md) |
| 3 | materials and images | ⬜ | v0.4.0 (planned) |
| 4 | cameras, lights and collections | ⬜ | v0.5.0 (planned) |
| 5 | object animation | ⬜ | v0.6.0 (planned) |
| 6 | armatures and UsdSkel | ⬜ | v0.7.0 (planned) |
| 7 | Blender host backend | ⬜ | v0.8.0 (planned) |
| 8 | performance, robustness and deferred older-Blender compatibility | ⬜ | v0.9.0 (planned) |

v1.0.0 is the first stable reader contract
([DESIGN_POLICY.md §14.1](../design/DESIGN_POLICY.md#141-first-stable-release)).
The remaining planned versions follow the 2026-10-01 implementation plan and
are re-decided when a Phase is ready to ship. The first public release
consolidates the already accepted native-reader and Mesh/Empty milestones.

See the [current tasks](current.md) for upcoming work and the
[capability matrix](../reference/CAPABILITY_MATRIX.md) for implemented behavior.

Phase 2 has a finite
[exit checklist](../design/DESIGN_POLICY.md#142-phase-2-exit-criteria),
accepted on 2026-10-05. Additional Blender 5.x release compatibility is
post-milestone work, not an open-ended requirement for closing that phase.
Passing local tests alone does not replace its hosted and integration gates.

## Open decisions

Every open question the design documents carry, in the order they block work.
The owning document holds the question and the proposed answer; this list only
schedules them.

| Id | Question | Owner | Blocks |
| --- | --- | --- | --- |
| STAGE-O3 | Color attributes and `displayColor` | [STAGE §18](../design/STAGE_CONTRACT.md#18-open-questions) | Phase 3 |
| MAT-O1 | Alpha mode mapping | [MATERIAL §9](../design/MATERIAL_POLICY.md#9-open-questions) | Phase 3 |
| MAT-O4 | Emission strength | [MATERIAL §9](../design/MATERIAL_POLICY.md#9-open-questions) | Phase 3 |
| STAGE-O4 | Parenting across scopes | [STAGE §18](../design/STAGE_CONTRACT.md#18-open-questions) | Phase 4 |
| STAGE-O6 | Light intensity conversion | [STAGE §18](../design/STAGE_CONTRACT.md#18-open-questions) | Phase 4 |
| STAGE-O7 | Camera lens and aperture units | [STAGE §18](../design/STAGE_CONTRACT.md#18-open-questions) | Phase 4 |
| STAGE-O8 | Where the `UsdSkelRoot` is | [STAGE §18](../design/STAGE_CONTRACT.md#18-open-questions) | Phase 6 |
| BACK-O1 | Host interchange format | [BACKEND §9](../design/BACKEND_POLICY.md#9-open-questions) | Phase 7 |
| BACK-O2 | Configuring Blender | [BACKEND §9](../design/BACKEND_POLICY.md#9-open-questions) | Phase 7 |
| BACK-O3 | License of the in-Blender script | [BACKEND §9](../design/BACKEND_POLICY.md#9-open-questions) | the first release with `blendHost` |
| STAGE-O2 | Shared meshes | [STAGE §18](../design/STAGE_CONTRACT.md#18-open-questions) | nothing (non-blocking) |
| BLEND-O3 | Linked libraries | [BLEND §12](../design/BLEND_CONTRACT.md#12-open-questions) | nothing (non-blocking) |
| NAME-O2 | Fallback names for non-Latin scenes | [NAMING §7](../design/NAMING_POLICY.md#7-open-questions) | nothing (non-blocking) |
| MAT-O2 | MaterialX target | [MATERIAL §9](../design/MATERIAL_POLICY.md#9-open-questions) | nothing (non-blocking) |
| MAT-O3 | Packed images | [MATERIAL §9](../design/MATERIAL_POLICY.md#9-open-questions) | nothing (non-blocking) |
| BACK-O4 | Evaluation-mode arguments | [BACKEND §9](../design/BACKEND_POLICY.md#9-open-questions) | nothing (non-blocking) |

## Quality bar (applies to every Phase)

- The file format reads only, and authors source data unless a backend says
  otherwise.
- The authored stage does not change meaning without a stage-contract bump.
- The dependency directions in
  [WORKSPACE.md §2](../architecture/WORKSPACE.md#2-dependency-directions) are
  enforced by CI, not by convention.
- Both build modes work.
- Every capability claim has a fixture behind it.
- Every documented command is one that has actually been run, and no document
  contradicts what CI does.

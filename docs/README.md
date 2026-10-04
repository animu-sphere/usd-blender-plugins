# usd-blender-plugins documentation

Documentation is organized by responsibility: each category answers one class
of question. The layout is the one `usd-mmd-plugins`, `usd-vrm-plugins` and
`open-strata` use, so the repositories read the same way.

For implemented behavior, use the
[capability matrix](reference/CAPABILITY_MATRIX.md). For phase status and
release mapping, use the [roadmap table](roadmap/README.md#status-at-a-glance);
for upcoming work, use the [current tasks](roadmap/current.md).

| Category | Answers | Start here |
| --- | --- | --- |
| [architecture/](architecture/) | How the workspace is structured: component identities, dependency directions, build modes, external dependencies. | [WORKSPACE.md](architecture/WORKSPACE.md) · [DEPENDENCIES.md](architecture/DEPENDENCIES.md) |
| [design/](design/) | What the file format reads, what it authors, and why. | [DESIGN_POLICY.md](design/DESIGN_POLICY.md) |
| [reference/](reference/) | Facts about the current tree: what is supported, which diagnostics exist. | [CAPABILITY_MATRIX.md](reference/CAPABILITY_MATRIX.md) · [DIAGNOSTICS.md](reference/DIAGNOSTICS.md) |
| [roadmap/](roadmap/) | Phase status, release mapping and incomplete tasks. | [README.md](roadmap/README.md) · [current.md](roadmap/current.md) |
| [contributing/](contributing/) | How to maintain these documents. | [documentation.md](contributing/documentation.md) |
| [guides/](guides/) | Commands verified against the implementation. | [building.md](guides/building.md) · [inspecting.md](guides/inspecting.md) |
| [reports/](reports/) | Dated evidence from real runs, not current capability or phase status. | [2026-10-05 compression policy measurements](reports/2026-10-05-compression-policy.md) |

`releases/` is created when it has real content
([contributing/documentation.md](contributing/documentation.md#category-ownership)).

## Canonical documents

- [design/DESIGN_POLICY.md](design/DESIGN_POLICY.md) is the **design policy**:
  the central rule (read the data a `.blend` stores, normalize it once into a
  Blender-neutral scene, author conventional OpenUSD, keep Blender's
  evaluation outside the file format), the component responsibilities, the
  schema admission test, the testing policy, the **Phase 0–8** sequence, the
  decisions frozen early, and where the design departs from the 2026-10-01
  implementation plan it was distilled from.
- Five focused contracts own one area each, and on that area they win over
  the design policy:
  - [design/STAGE_CONTRACT.md](design/STAGE_CONTRACT.md) — the exact authored
    stage: the `/Asset` hierarchy, metadata, the coordinate conversion,
    objects, meshes, cameras, lights, collections, skeletons, time, and the
    stage-contract version;
  - [design/BLEND_CONTRACT.md](design/BLEND_CONTRACT.md) — how `.blend` bytes
    are read (compression, header, blocks, SDNA, the ID graph) and what each
    source concept becomes in the Scene IR;
  - [design/MATERIAL_POLICY.md](design/MATERIAL_POLICY.md) — Principled BSDF
    to `UsdPreviewSurface`, the supported node subset, images and binding, and
    the later MaterialX path;
  - [design/NAMING_POLICY.md](design/NAMING_POLICY.md) — source names versus
    USD identifiers, collisions, deterministic order, asset paths;
  - [design/BACKEND_POLICY.md](design/BACKEND_POLICY.md) — the native backend,
    the process-isolated Blender host backend, source versus evaluated data,
    Blender as a test oracle, and the license boundary.
- [architecture/WORKSPACE.md](architecture/WORKSPACE.md) is the binding
  **workspace contract**. When a document disagrees with it about structure,
  it wins, and structural changes go there first, in their own pull request.

## Source-of-truth rules

- Code is authoritative for implemented behavior; `architecture/` and
  `reference/` record it and change with it.
- `design/` defines intended contracts, not capability or delivery status.
- Phase status and release mapping appear only in the
  [roadmap table](roadmap/README.md#status-at-a-glance); incomplete task status
  appears only in [current.md](roadmap/current.md).
- The details are in [contributing/documentation.md](contributing/documentation.md).

## Contributing

Repository development and review expectations are in
[CONTRIBUTING.md](../CONTRIBUTING.md), community standards in
[CODE_OF_CONDUCT.md](../CODE_OF_CONDUCT.md), and private vulnerability reporting
in [SECURITY.md](../SECURITY.md).

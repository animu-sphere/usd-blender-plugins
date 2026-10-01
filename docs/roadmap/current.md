# Current milestones

The next two Phases, broken into tasks. A task leaves this page when it lands;
a Phase leaves it when every task has. Scope is
[DESIGN_POLICY.md §14](../design/DESIGN_POLICY.md#14-phases); the release a
Phase lands in is the [status table](README.md#status-at-a-glance).

## Phase 0 — workspace skeleton 🚧

Goal: `.blend` is recognized as an OpenUSD file format, and opens as the
minimal `/Asset` stage.

| # | Task | Done when | Status |
| --- | --- | --- | --- |
| 0.1 | Finish repository policy files: `LICENSE` (Apache-2.0), `CONTRIBUTING.md`, `SECURITY.md`, `CODE_OF_CONDUCT.md` | the files exist; version, changelog, README, `.gitattributes` and root `.clang-format` already exist | 🚧 |
| 0.5 | Finish `openstrata.ci.yaml` | the root CMake, presets, modules and workspace manifest already build through plain CMake and `ost`; the CI matrix exists | 🚧 |
| 0.7 | Validate the minimal stage on a Blender-written `empty.blend` | [STAGE_CONTRACT.md §17](../design/STAGE_CONTRACT.md#17-validation-checklist) items 1-5 already pass on synthetic headers; repeat on a real fixture | 🚧 |
| 0.8 | Add the Blender fixture generator for `empty.blend` | byte-level fixtures already regenerate identically; the Blender fixture does too | 🚧 |
| 0.9 | CI on Windows and Linux; `ost plugin build`, `doctor`, `test` L0-L5 | local Windows tests pass; CI is green on both | ⬜ |

## Phase 1 — container and SDNA ⬜

Goal: `.blend` binary structure is read by independent C++, stably, and can be
looked at.

| # | Task | Done when | Status |
| --- | --- | --- | --- |
| 1.1 | Resolve BLEND-O1, BLEND-O2, BLEND-O4, BLEND-O5, DEP-O2 | the owning documents record the answers | ⬜ |
| 1.2 | Compression: gzip and Zstandard, with size and ratio limits | compressed and uncompressed fixtures read identically; a bomb fixture fails with `BLEND_COMPRESSION_*` | ⬜ |
| 1.3 | Container readers: legacy and Blender 5 block layouts, normalized to one block record | fixtures from 4.5 LTS and 5.x enumerate the same block kinds | ⬜ |
| 1.4 | SDNA: `NAME`, `TYPE`, `TLEN`, `STRC`; member offsets; member lookup by name | every struct in each fixture's SDNA decodes; malformed SDNA fixtures fail with `BLEND_DNA_*` | ⬜ |
| 1.5 | Pointer map and raw datablock enumeration | every ID block in each fixture is listed with its type and name | ⬜ |
| 1.6 | `blend_inspect`: summary, `--blocks`, `--dna`, `--objects` | the tool reports version and datablock counts for every fixture, e.g. `Objects: 12, Meshes: 5` | ⬜ |
| 1.7 | Malformed-input tests: truncation at every block boundary, oversized lengths, invalid indices | no crash, no over-read, every failure a diagnostic | ⬜ |
| 1.8 | `guides/inspecting.md` | every command in it has been run | ⬜ |

## After Phase 1

Phase 2 builds `blendScene` and reaches the first useful milestone: **a
`.blend` containing a cube opens directly in `usdview` as a `UsdGeomMesh`**
at `/Asset/geo/Cube/mesh`. Its tasks are written here when Phase 1 is nearly
done.

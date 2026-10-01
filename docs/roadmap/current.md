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
| 1.2 | Compression: gzip and Zstandard, with size and ratio limits | compressed and uncompressed fixtures read identically; a bomb fixture fails with `BLEND_COMPRESSION_*` | 🚧 |
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

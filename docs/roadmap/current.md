# Current milestones

The next two Phases, broken into tasks. A task leaves this page when it lands;
a Phase leaves it when every task has. Scope is
[DESIGN_POLICY.md §14](../design/DESIGN_POLICY.md#14-phases); the release a
Phase lands in is the [status table](README.md#status-at-a-glance).

## Phase 0 — workspace skeleton ⬜

Goal: `.blend` is recognized as an OpenUSD file format, and opens as the
minimal `/Asset` stage.

| # | Task | Done when | Status |
| --- | --- | --- | --- |
| 0.1 | Repository files: `LICENSE` (Apache-2.0), `VERSION`, `CHANGELOG.md`, `README.md`, `CONTRIBUTING.md`, `SECURITY.md`, `CODE_OF_CONDUCT.md`, `.gitattributes`, `.clang-format` | the files exist and the README links the docs | ⬜ |
| 0.2 | Resolve DEP-O1 and STAGE-O5 | the owning documents record the answers | ⬜ |
| 0.3 | Scaffold the bundle from OpenStrata's `usd-fileformat-cpp` template (`ost plugin new usd-fileformat … --extension blend`), and move it to `plugins/usdBlendFileFormat/` | `ost plugin inspect` passes; identities match [WORKSPACE.md §1](../architecture/WORKSPACE.md#1-identities) | ⬜ |
| 0.4 | Update the manifest: name, license, the pinned OpenUSD runtime, `usd-fileformat:blend` | the manifest matches [WORKSPACE.md §4](../architecture/WORKSPACE.md#4-manifests-versioning-and-build-metadata) | ⬜ |
| 0.5 | Root `CMakeLists.txt`, `CMakePresets.json`, `cmake/` modules, `openstrata.toml`, `openstrata.ci.yaml` | plain CMake and `ost` both build the bundle | ⬜ |
| 0.6 | `libs/blendFile` scaffold: `ByteSource`, `FileByteSource`, `MemoryByteSource`, the diagnostic record and `Result<T>`, legacy header recognition | unit tests pass; the boundary test shows no OpenUSD in its link line | ⬜ |
| 0.7 | `UsdBlendFileFormat::Read`: `ArAssetByteSource`, header validation, `/Asset`, `geo`, `mtl`, layer metadata, contract version | [STAGE_CONTRACT.md §17](../design/STAGE_CONTRACT.md#17-validation-checklist) items 1–5 pass on `empty.blend`; a non-`.blend` file fails with `BLEND_HEADER_*` | ⬜ |
| 0.8 | Fixture generators: byte-level (invalid header, truncated), and a Blender script for `empty.blend` | committed fixtures equal what the generators write | ⬜ |
| 0.9 | CI on Windows and Linux; `ost plugin build`, `doctor`, `test` L0–L5 | CI is green on both | ⬜ |
| 0.10 | `guides/building.md` with the commands actually run | every command in it has been run | ⬜ |

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

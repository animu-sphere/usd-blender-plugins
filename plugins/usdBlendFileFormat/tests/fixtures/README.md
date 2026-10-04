# Blend fixtures

## Synthetic headers

The six header fixtures are synthetic bytes, not Blender-generated scenes. `header_only.blend`
contains only the twelve-byte legacy header: it proves header identification,
not container, SDNA, or Blender 4.5 scene support. Full and metadata-only
importer reads now reject it with `BLEND_BLOCK_MISSING_ENDB`; the other five
headers remain malformed-input tests.
Reader and USD support are recorded in the
[capability matrix](../../../../docs/reference/CAPABILITY_MATRIX.md).

Regenerate from the repository root:

```powershell
./tests/fixtures/generate.ps1
./tests/fixtures/generate.ps1 -Check
```

## Blender-written empty scene

`empty.blend` is an uncompressed format-1 file written by Blender 5.2.2 LTS
(build `d13f752e3b9c`) using this repository's
[`generate_blender.py`](../../../../tests/fixtures/generate_blender.py).
It contains one scene named `Scene`, unit scale 1, and no objects, meshes,
cameras, lights, materials, collections, linked libraries or actions.

The generator requires Blender 5.2.2, resets factory settings, and uses
`bpy.data.libraries.write` to save the Scene and its dependencies without
workspaces or screens. The Scene has a fake user so it is retained. Blender
reports `Library file, loading empty scene` when opening this scene-library
file; the stored Scene is nevertheless restored and becomes active. A
regression test checks this using a stored non-default unit scale.

`--check` reopens and checks the existing file without changing it.
`--check-bytes` also regenerates into a temporary directory and compares the
bytes. Commands and the regression test invocation are in
[the build guide](../../../../docs/guides/building.md#fixture-reproducibility).

Three independent processes produced identical bytes on the verified Windows
build; the regression tests also compare different output paths and ensure
both checks are non-destructive. The fixture is 188,558 bytes with SHA256
`a364e1177a58f78362b44d8b03461eb8092f0613c67813e1d56d9d1b34c6e7b6`.
This evidence is scoped to that Windows build, not cross-platform byte
identity. The native active-Scene selector and registered importer reject this
library with `BLEND_SCENE_ACTIVE_MISSING`, without a first-Scene fallback.

This fixture exercises container and SDNA syntax and missing-active-Scene
diagnostics; it is not a successful stage fixture.

## Normal-save cube

`single_cube.blend` is an uncompressed format-1 normal save generated on
Windows on 2026-10-04 with Blender 5.2.2 LTS (`d13f752e3b9c`) by this
repository's [generate_cube.py](../../../../tests/fixtures/generate_cube.py).
It contains one active `Scene`, unit scale 1, one visible identity-transform
`Cube`, eight points spanning two meters per axis, six quads, 24 corners,
flat corner normals and a render `UVMap`. File-browser directories use `//`.
It is repository-generated test data under the repository license.

The stored fixture is 493,671 bytes, SHA-256
`997e8855114784f7da8308350677ad39141a291755459b7e0c448f7db343dbec`.
Full-save bytes are not claimed reproducible. `--check` regenerates only in a
temporary directory and compares semantic points/topology/normals/UVs after
reopening the committed file, without rewriting it.

The bundle's smoke and roundtrip pyramid uses this fixture.
`single_cube.blend.golden.usda` pins the normalized flattened stage at
`/Asset/geo/Cube/mesh`, including exact polygon topology, normals, indexed `st`,
extent and identity transform. `test_stage.py` additionally checks metadata,
reference composition, repeat-read determinism, metadata-only output,
container/SDNA failures, compressed rejection and recoverable diagnostics.
Commands are in the [build guide](../../../../docs/guides/building.md#registered-cube-fixture).
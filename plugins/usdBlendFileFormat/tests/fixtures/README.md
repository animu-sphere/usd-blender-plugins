# Blend fixtures

## Synthetic headers

The six header fixtures are synthetic bytes, not Blender-generated scenes. `header_only.blend`
contains only the twelve-byte legacy header: it proves Phase 0 registration
and stage scaffolding, not container, SDNA, or Blender 4.5 scene support.
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
identity. The plugin's repeat-read and golden tests separately verify
deterministic USD output.

This fixture exercises the minimal stage contract on a Blender-written file;
it does not by itself establish block, SDNA or scene compatibility.
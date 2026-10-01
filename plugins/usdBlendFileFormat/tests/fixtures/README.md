# Blend fixtures

## Synthetic headers

The six header fixtures are synthetic bytes, not Blender-generated scenes. `header_only.blend`
contains only the twelve-byte legacy header: it proves Phase 0 registration
and stage scaffolding, not container, SDNA, or Blender 4.5 scene support.
The parser does not yet validate bytes after the header.

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

The generator requires Blender 5.2.2, resets factory settings, disables backup
saves, writes the file, then reopens it in Blender to verify these invariants.
`--check` reopens and checks the existing file without changing it. Commands
are in [the build guide](../../../../docs/guides/building.md#fixture-reproducibility).

Separate Blender processes produce files of the same size but different bytes.
`--check` verifies scene content, not byte-identical regeneration; roadmap task
0.8 remains incomplete. The plugin's repeat-read and golden tests verify
deterministic USD output, not deterministic Blender serialization.

This fixture proves the minimal stage contract on a real file. The reader
still stops at the header; block, SDNA and scene decoding remain unimplemented.
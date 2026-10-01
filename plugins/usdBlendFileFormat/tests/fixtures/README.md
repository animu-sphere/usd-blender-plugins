# Header fixtures

These are synthetic bytes, not Blender-generated scenes. `header_only.blend`
contains only the twelve-byte legacy header: it proves Phase 0 registration
and stage scaffolding, not container, SDNA, or Blender 4.5 scene support.
The parser does not yet validate bytes after the header.

Regenerate from the repository root:

```powershell
./tests/fixtures/generate.ps1
./tests/fixtures/generate.ps1 -Check
```
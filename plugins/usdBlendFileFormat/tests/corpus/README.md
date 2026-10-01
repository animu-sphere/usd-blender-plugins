# Blender corpus

These are contributor-provided Blender files, not fixtures regenerated from
committed scripts. Keep their original bytes and group them by the reported
Blender version. Synthetic header fixtures remain in `../fixtures/`.

Each version directory has a `manifest.json` recording the reported Blender
version and its evidence, relative file paths, byte sizes, SHA-256 hashes,
provenance, explicit commit permission, and reader evidence. Commit permission
is not a declaration of a separate asset license.

| File | Reported Blender version | Commit permission | Stored format |
| --- | --- | --- | --- |
| [blender-4.5.13/Untitled.blend](blender-4.5.13/Untitled.blend) | 4.5.13 LTS | 2026-10-02 | uncompressed legacy, 64-bit pointers, little-endian |
| [blender-5.2.2/Untitled.blend](blender-5.2.2/Untitled.blend) | 5.2.2 LTS | 2026-10-01 | Zstandard-compressed format-1 |

Patch versions and LTS labels are contributor-reported, not independently
verified; headers establish only versions 405 and 502. No generation scripts
are available and no scene contents are assumed. Original bytes are preserved.

## Real-file comparison

On Windows on 2026-10-02, `blendFile.header` verified both headers, full-file
bytes, generated gzip/Zstandard byte round trips and block framing through
terminal `ENDB`. The original 4.5.13 file and its repository copy have identical
SHA-256 hashes. Its 1,240 blocks, `DNA1` payload at 372,491 (132,244 bytes), and
`ENDB` ending at 504,759 were also checked by an independent legacy byte walk.
The regression fixes these positions and compares the complete block-kind
sets from both corpus files; all 20 kinds match:

```text
CA DATA DNA1 ENDB GLOB GR IM LA LS MA ME OB PL REND SC SN TEST WM WO WS
```

The [4.5.13 manifest](blender-4.5.13/manifest.json) records its 504,759-byte
size, SHA-256, permission and reader evidence; the
[5.2.2 manifest](blender-5.2.2/manifest.json) records the other input.
These checks do not decode SDNA or establish scene compatibility, do not
provide a Blender-written gzip file, and leave the importer header-only.
This comparison was run locally on Windows; Linux execution remains
unverified. Commands are in the
[build guide](../../../../docs/guides/building.md#reader-through-openstrata).
Supported behavior is recorded in the
[capability matrix](../../../../docs/reference/CAPABILITY_MATRIX.md).
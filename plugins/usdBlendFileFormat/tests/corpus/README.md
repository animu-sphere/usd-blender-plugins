# Blender corpus

These are contributor-provided Blender files, not fixtures regenerated from
committed scripts. Keep their original bytes and group them by the reported
Blender version. Synthetic header fixtures remain in `../fixtures/`.

Each version directory has a `manifest.json` recording the reported Blender
version and its evidence, relative file paths, byte sizes, SHA-256 hashes,
provenance, explicit commit permission, and reader evidence. Commit permission
is not a declaration of a separate asset license.

`blender-5.2.2/Untitled.blend` was supplied with commit permission on
2026-10-01. Blender **5.2.2 LTS** is contributor-reported, not independently
verified; the file header alone cannot establish a patch version or LTS label.
No generation script is available and no scene contents are assumed.

The `blendFile.header` regression exercises this Zstandard-compressed format-1
header and expects file version 502. A header test does not establish Blender
5.2 scene compatibility or validate the full compressed file. Supported
behavior is recorded in the
[capability matrix](../../../../docs/reference/CAPABILITY_MATRIX.md).
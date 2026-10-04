# usdBlendFileFormat - OpenUSD blend file-format plugin

Scaffolded from the official `usd-fileformat-cpp` template with `ost` 0.23.14.
The bundle owns the OpenUSD file-format boundary and resolver-backed byte
access, using the independent `blendFile` reader and `blendScene` IR. Supported behavior belongs
to the [capability matrix](../../docs/reference/CAPABILITY_MATRIX.md); phase
status belongs to the [roadmap table](../../docs/roadmap/README.md#status-at-a-glance).

## Layout

```
openstrata.plugin.yaml          bundle contract (identity, runtime range, provides, tests)
CMakeLists.txt                  builds libUsdBlendFileFormatFileFormat.so into lib/
cmake/OpenStrataPlugin.cmake    pinned, self-contained build/install mechanics
src/UsdBlendFileFormatFileFormat.{h,cpp}  the SdfFileFormat implementation
src/AuthorScene.{h,cpp}          internal normalized Scene IR-to-USD authoring
plugin/resources/usdBlendFileFormat/plugInfo.json   USD plugin registration
tests/fixtures/                 synthetic headers, Blender scene, negatives and stage goldens
tests/AuthorSceneTests.cpp       synthetic and native/oracle USD authoring CTest
```

The copied CMake helper is versioned with this scaffold and requires neither an
OpenStrata checkout nor `ost` at build time. Keep bundle-specific targets,
components, resources, and tests in `CMakeLists.txt`; update helper mechanics by
reviewing a newer template rather than linking to the generator source tree.

## Workflow

```sh
ost plugin build plugins/usdBlendFileFormat
ost plugin inspect plugins/usdBlendFileFormat
ost plugin doctor plugins/usdBlendFileFormat
ost plugin test plugins/usdBlendFileFormat
```

Run from the repository root. The OpenStrata runtime and plain CMake SDK rules
are in the [dependency contract](../../docs/architecture/DEPENDENCIES.md#1-openusd).
For standalone CMake
configuration, install `blendFile` and `blendScene` first and supply their prefixes alongside
the SDK. See [the build guide](../../docs/guides/building.md) for verified
commands and [the capability matrix](../../docs/reference/CAPABILITY_MATRIX.md)
for implementation status.

`usdBlend.authoring` tests the internal Scene IR boundary independently from
`SdfFileFormat::Read`. The importer remains header-only until native input
selection and production limits are wired; this build does not yet open
`.blend` geometry through `usdview`.

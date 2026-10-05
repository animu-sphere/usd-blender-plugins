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
src/ReadScene.{h,cpp}            bounded byte-to-native-Scene importer composition
plugin/resources/usdBlendFileFormat/plugInfo.json   USD plugin registration
tests/fixtures/                 synthetic headers, Blender scene, negatives and stage goldens
tests/AuthorSceneTests.cpp       synthetic and native/oracle USD authoring CTest
tests/ReadSceneTests.cpp         compressed importer equivalence and limit CTest
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
`SdfFileFormat::Read`. `usdBlend.importer` tests the bounded byte-to-Scene
composition and exact compressed-input limit boundaries. The registered
importer composes native decoding and USD authoring for the fixture-backed
Mesh/Empty scope, including gzip and Zstandard under the accepted
[compression policy](../../docs/design/BLEND_CONTRACT.md#42-standard-full-stream-limit-policy).
The reader API and inspection CLI still require explicit compression limits.

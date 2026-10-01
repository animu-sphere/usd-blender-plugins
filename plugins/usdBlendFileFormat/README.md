# usdBlendFileFormat - OpenUSD blend file-format plugin

Scaffolded from the official `usd-fileformat-cpp` template with `ost` 0.23.14.
The bundle now reads bytes through ArResolver, validates a legacy header in
the standalone `blendFile` library, and authors the minimal `/Asset` stage.
It does not decode Blender blocks, SDNA or scenes yet.

## Layout

```
openstrata.plugin.yaml          bundle contract (identity, runtime range, provides, tests)
CMakeLists.txt                  builds libUsdBlendFileFormatFileFormat.so into lib/
cmake/OpenStrataPlugin.cmake    pinned, self-contained build/install mechanics
src/UsdBlendFileFormatFileFormat.{h,cpp}  the SdfFileFormat implementation
plugin/resources/usdBlendFileFormat/plugInfo.json   USD plugin registration
tests/fixtures/                 synthetic headers, negatives and a stage golden
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

Run from the repository root. The OpenStrata verification runtime is pinned
to 26.08; plain CMake accepts the caller's installed OpenUSD SDK without a
version restriction. Other releases are unverified. For standalone CMake
configuration, install `blendFile` first and supply its prefix alongside
the SDK. See [the build guide](../../docs/guides/building.md) for verified
commands and [the capability matrix](../../docs/reference/CAPABILITY_MATRIX.md)
for implementation status.

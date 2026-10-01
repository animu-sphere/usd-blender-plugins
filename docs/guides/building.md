# Building and testing

Verified on Windows on 2026-10-01 with `ost` 0.23.14, Visual Studio 2026
(MSVC 19.51), and the local OpenStrata `cy2026` / `usd` OpenUSD 26.08 artifact.
Linux and the planned MSVC 2022 toolchain have not been exercised yet.
Run commands from the repository root.

`ost` generates a machine-local `strata.lock`; it is ignored until a
cross-platform locking policy is established with the CI matrix.

## Reader without OpenUSD

```powershell
cmake --preset reader
cmake --build --preset reader
ctest --preset reader
```

Five CTests are registered: byte-source/header behavior, forbidden-include
scanning, generated link metadata setup, generated link boundary inspection,
and link-boundary rejection checks. The library has no external link
dependencies. The metadata setup fixture reconfigures the existing build;
CMake reads the File API query on this second configure, then the link gate
checks the selected configuration's generated libraries, including transitive
dependencies. With a multi-config generator, the `CMAKE_BUILD_TYPE` preset
value is unused; the build/test presets select Release explicitly.

The link-boundary scripts pass on generated MSVC Release metadata in both
root reader-only and standalone library builds. Rejection checks pass for
OpenUSD, Blender, Scene IR and unknown libraries, and missing reader link
information. CMake Tools currently has no active configure preset; select
`reader` there to use its build/test integration. OpenStrata's library commands
do not depend on that editor selection and have run the full updated suite.

## Reader through OpenStrata

```powershell
ost library build libs/blendFile --target cy2026 --profile usd
ost library test libs/blendFile --target cy2026 --profile usd --filter 'blendFile\.link'
ost library test libs/blendFile --target cy2026 --profile usd
```

Verified on Windows with Ninja and MSVC 19.51. Build and installation pass;
the filtered run passes all three link tests, including the metadata setup
fixture. The full run passes all five tests, including legacy and Blender 5
headers, the contributor-provided Zstandard-compressed Blender file, malformed
headers and streams, frame-boundary splits, decoder-window and input limits,
and failed source reads. This is header-only verification, not container or
scene decoding. The plugin pyramid below does not run these reader CTests.

## OpenStrata bundle

```powershell
ost plugin build plugins/usdBlendFileFormat --target cy2026 --profile usd
ost plugin inspect plugins/usdBlendFileFormat
ost plugin doctor plugins/usdBlendFileFormat
ost plugin test --workspace --graph-only
ost plugin test plugins/usdBlendFileFormat --target cy2026 --profile usd
ost plugin run plugins/usdBlendFileFormat --target cy2026 --profile usd -- python plugins/usdBlendFileFormat/tests/test_stage.py
```

The manifest declares the `blendFile` edge; `ost` builds and installs the
library into its workspace prefix before configuring the standalone bundle.
L0-L5 report 12 passes, zero failures; four checks are skipped because there
are no extra runtime paths or Python ABI declaration and C++ ABI is inherited.
The four stage tests assert hierarchy, metadata, diagnostic codes, repeat-read
determinism and contract-version preservation through a reference.

The six fixtures are synthetic legacy headers, not valid complete Blender
scenes. The current parser deliberately stops after the header. L5 compares
the flattened minimal stage against a golden; the generated source comment
has its path removed by `ost` normalization. USDA files must use LF endings.

## Plain CMake with the installed SDK

The prefix below names the local artifact used in this run, not a required
machine path or SDK version. Another installed OpenUSD SDK may be supplied;
only 26.08 has been verified so far. Build and load against the same release.

```powershell
cmake -S . -B build/usd-vs18 -G "Visual Studio 18 2026" -A x64 "-DCMAKE_PREFIX_PATH=$HOME/.ost/runtimes/openstrata-cy2026-windows-x86_64-py313-usd"
cmake --build build/usd-vs18 --config Release
ctest --test-dir build/usd-vs18 -C Release --output-on-failure
```

This builds the reader first, resolves OpenUSD once through
`find_package(pxr CONFIG REQUIRED)` without a version constraint, then builds
the plugin. The original header and include CTests passed on the verified SDK;
the expanded five-test suite has not been rerun in this mode.

## Fixture reproducibility

```powershell
./tests/fixtures/generate.ps1
./tests/fixtures/generate.ps1 -Check
```

All six fixture byte arrays match the generator. A Blender-written
`empty.blend` and its generator remain Phase 0 work.
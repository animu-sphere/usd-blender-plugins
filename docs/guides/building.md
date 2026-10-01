# Building and testing

The local commands below were exercised on Windows on 2026-10-01 with
`ost` 0.23.14, Visual Studio 2026
(MSVC 19.51), and the local OpenStrata `cy2026` / `usd` OpenUSD 26.08 artifact.
This records the local command environment, not current platform or CI status;
delivery status is in the [roadmap table](../roadmap/README.md#status-at-a-glance).
Run commands from the repository root.

`ost` generates a machine-local `strata.lock`; it is ignored until a
cross-platform locking policy is established with the CI matrix.

## CI matrix

[openstrata.ci.yaml](../../openstrata.ci.yaml) pins `ost` 0.23.14 and
digest-pinned OpenUSD 26.08 `gl` SDKs for Windows and Linux. The runners are
GitHub-hosted `windows-2022` (MSVC 2022) and `ubuntu-24.04`, with host Python
3.13. Linux cells install `libx11-dev` and `libxt-dev`, required by the
runtime's exported MaterialX package even for a non-imaging consumer.

The five pull-request cells are:

- one workspace graph check, without materializing a runtime;
- two root CMake build/CTest checks, including all five reader tests and
	their include/link boundary gates;
- two standalone bundle build, L0-L5 and package checks, exercising the
	manifest's installed `blendFile` dependency.

The generated workflow is
[ost-source-ci.yml](../../.github/workflows/ost-source-ci.yml). Change the
matrix and regenerate it; do not hand-edit generated jobs. It uses read-only
repository permissions, no secrets and no publication. Hosted jobs can incur
GitHub Actions charges, as acknowledged by the runner declarations.

```powershell
ost ci validate
ost ci plan
ost ci generate github
```

When replacing an existing workflow, add `--force` to the generation command.
Runtime resolution depends on artifacts available to the selected host and
registry; a missing local registry entry is not a hosted CI result.

The generated bundle pyramid is
not a separate `ost plugin doctor` invocation, and neither generated job kind
runs `test_stage.py`. The hand-maintained companion workflow
[stage-contract-ci.yml](../../.github/workflows/stage-contract-ci.yml) resolves
the existing bundle cells with `ost ci matrix`, without copying their SDK
digests, runners or host Python/package requirements. It builds the standalone
bundle on both hosts, runs explicit `ost plugin doctor` diagnostics and all
five stage-contract tests, and uploads their reports and logs. These additional
build jobs also use billed hosted infrastructure; they do not publish anything
or use secrets. The resolver bootstrap is pinned to the matrix's `ost` version
and rejects version drift; update both when changing that pin. Do not pass this
companion workflow to the generator.

The regular jobs use committed fixtures and do not install or run Blender.

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

The link-boundary rejection checks cover OpenUSD, Blender, Scene IR and unknown
libraries, and missing reader link information. Select the `reader` configure
preset in CMake Tools to use its build/test integration. OpenStrata's library
commands do not depend on that editor selection.

## Reader through OpenStrata

```powershell
ost library build libs/blendFile --target cy2026 --profile usd
ost library test libs/blendFile --target cy2026 --profile usd --filter 'blendFile\.link'
ost library test libs/blendFile --target cy2026 --profile usd
```

The filtered invocation selects all three link tests, including the metadata
setup fixture. The full suite covers legacy and Blender 5
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
The five stage tests assert hierarchy, metadata, diagnostic codes, repeat-read
determinism and contract-version preservation through a reference. Both the
synthetic header and Blender-written empty scene exercise the stage contract.

Six fixtures are synthetic legacy headers; `empty.blend` is a complete,
uncompressed file written by Blender 5.2.2 LTS. L3/L4 use the real fixture;
L5 compares both flattened
minimal stages against their goldens. The generated source comment has its
path removed by `ost` normalization. USDA files must use LF endings.

## Plain CMake with the installed SDK

The prefix below names the local artifact used in the dated local run, not a
required machine path or SDK version. Another installed OpenUSD SDK may be
supplied under the [dependency contract](../architecture/DEPENDENCIES.md#1-openusd).
Build and load against the same release.

```powershell
cmake -S . -B build/usd-vs18 -G "Visual Studio 18 2026" -A x64 "-DCMAKE_PREFIX_PATH=$HOME/.ost/runtimes/openstrata-cy2026-windows-x86_64-py313-usd"
cmake --build build/usd-vs18 --config Release
ctest --test-dir build/usd-vs18 -C Release --output-on-failure
```

This builds the reader first, resolves OpenUSD once through
`find_package(pxr CONFIG REQUIRED)` without a version constraint, then builds
the plugin. CTest runs the root build's registered tests.

## Fixture reproducibility

```powershell
./tests/fixtures/generate.ps1
./tests/fixtures/generate.ps1 -Check
```

`-Check` compares all six synthetic fixture byte arrays with the generator.

The Blender fixture generator requires Blender 5.2.2 LTS (verified build
`d13f752e3b9c`). On the verified Windows installation:

```powershell
$blender = Join-Path $env:ProgramFiles 'Blender Foundation/Blender 5.2/blender.exe'
& $blender --background --factory-startup --disable-autoexec --python-exit-code 1 --python tests/fixtures/generate_blender.py
& $blender --background --factory-startup --disable-autoexec --python-exit-code 1 --python tests/fixtures/generate_blender.py -- --check
& $blender --background --factory-startup --disable-autoexec --python-exit-code 1 --python tests/fixtures/generate_blender.py -- --check-bytes
& $blender --background --factory-startup --disable-autoexec --python-exit-code 1 --python tests/fixtures/test_generate_blender.py
```

Generation writes `plugins/usdBlendFileFormat/tests/fixtures/empty.blend` using
the library writer, saving the Scene and its dependencies but no UI state,
then reopens it to verify an empty scene with unit scale 1. Blender reports
`Library file, loading empty scene`; the saved Scene is restored and inspected,
not replaced with an unchecked default. See the
[fixture provenance](../../plugins/usdBlendFileFormat/tests/fixtures/README.md#blender-written-empty-scene)
for its size and checksum.

`--check` validates the committed file's content without modifying it.
`--check-bytes` first generates a temporary file and compares its bytes, then
validates the content. `--output <path.blend>` selects a different destination
for generation or either check. The two check modes are mutually exclusive.

The regression suite checks independent-process and different-path
reproduction, non-destructive checks and modified-byte rejection, stored Scene
validation, and synthetic-header rejection. Generation evidence and its
platform scope belong to the fixture provenance linked above.
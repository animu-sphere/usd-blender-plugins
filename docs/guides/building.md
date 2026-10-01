# Building and testing

Verified on Windows on 2026-10-01 with `ost` 0.23.14, Visual Studio 2026
(MSVC 19.51), and the local OpenStrata `cy2026` / `usd` OpenUSD 26.08 artifact.
Linux and the planned MSVC 2022 toolchain have not been exercised yet.
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

These commands pass locally on Windows; regeneration also matches the
checked-in workflow. When replacing an existing workflow, add `--force` to
the generation command. `ost ci validate --resolve` was also run: the Windows
runtime resolves locally, but the Linux digest is not in the local registry,
so that check fails. This does not verify its remote availability or execution.

Hosted Windows/Linux jobs have not yet run. The generated bundle pyramid is
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

The explicit doctor and stage-test commands pass locally on Windows. Hosted
results for both workflows remain unverified under task 0.9.
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
The five stage tests assert hierarchy, metadata, diagnostic codes, repeat-read
determinism and contract-version preservation through a reference. Both the
synthetic header and Blender-written empty scene exercise the stage contract.

Six fixtures are synthetic legacy headers; `empty.blend` is a complete,
uncompressed file written by Blender 5.2.2 LTS. The current parser deliberately
stops after the header. L3/L4 use the real fixture; L5 compares both flattened
minimal stages against their goldens. The generated source comment has its
path removed by `ost` normalization. USDA files must use LF endings.

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

All six synthetic fixture byte arrays match the generator.

The Blender fixture generator requires Blender 5.2.2 LTS (verified build
`d13f752e3b9c`). On the verified Windows installation:

```powershell
$blender = Join-Path $env:ProgramFiles 'Blender Foundation/Blender 5.2/blender.exe'
& $blender --background --factory-startup --disable-autoexec --python-exit-code 1 --python tests/fixtures/generate_blender.py
& $blender --background --factory-startup --disable-autoexec --python-exit-code 1 --python tests/fixtures/generate_blender.py -- --check
```

Generation writes `plugins/usdBlendFileFormat/tests/fixtures/empty.blend`, then
reopens it in Blender to verify an empty scene with unit scale 1. `--check`
validates the committed file without modifying it. `--output <path.blend>`
selects a different destination for either mode.

Two separate Blender runs produced 487,593-byte files with different SHA256
values. Scene-content validation and deterministic USD output pass, but
byte-identical Blender regeneration has not been achieved; task 0.8 remains
in progress. No Blender 4.5 fixture or Linux run is claimed.
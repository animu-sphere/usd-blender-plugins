# Building and testing

The local commands below were exercised on Windows on 2026-10-01 and 2026-10-02 with
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
- two root CMake build/CTest checks, including the reader and Scene IR tests
	and their include/link boundary gates, plus the inspection CLI test;
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

The five reader CTests cover byte-source/header/compression/block/SDNA/raw-ID/value-view behavior, forbidden-include
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
setup fixture. The full suite covers legacy and Blender 5 headers, synthetic
gzip streams, the contributor-provided Zstandard-compressed Blender file,
malformed headers and streams, member/frame splits, and failed source reads.
Probe cases cover decoder-window/input limits and stopping before trailing
checksum validation. Full-stream byte cases cover final checksums,
truncation, metadata across chunks, trailing garbage, exact input/output/ratio
limits, high-ratio streams, and byte-identical gzip/Zstandard round trips of
the generated empty scene and both decoded corpus files. Their explicit limits are
test budgets, not production defaults.

The same test prints and pins the committed files' stored/decoded sizes,
minimum integer expansion ratio and minimum accepted decoder-window log.
Exact byte budgets must preserve all decoded bytes; reducing an input/output
limit by one, or the compressed file's ratio/window allowance below its
minimum, must produce the matching `BLEND_COMPRESSION_*` diagnostic. See the
[compression measurements](../../plugins/usdBlendFileFormat/tests/corpus/README.md#compression-measurements)
for values, measurement semantics and corpus limitations.

Block cases enumerate both legacy pointer widths and byte orders and the
format-1 layout, including the generated empty scene, the real uncompressed
4.5.13 legacy corpus and the decoded 5.2.2 corpus. The two corpus files'
complete block-kind sets are compared; the 4.5.13 block count and `DNA1`/`ENDB`
positions are fixed by the regression. See the
[corpus comparison evidence](../../plugins/usdBlendFileFormat/tests/corpus/README.md#real-file-comparison).
They cover contiguous unaligned payloads, large 64-bit fields in a sparse
source, exact count limits, negative fields, truncated boundaries, oversized
lengths, source read failures, unknown-code diagnostics, and terminal `ENDB`
checks. Other legacy pointer-width/byte-order combinations remain synthetic.

Malformed-block checks cut each complete fixture and synthetic container at
every block boundary and every partial block-header byte, and at the start,
middle and final byte of each nonempty payload. Each block's length is also
replaced with a value exceeding the remaining bytes and the layout's maximum
positive signed length. A checked byte source rejects out-of-range read
requests; every failure must carry the expected fatal, non-recoverable code,
byte offset and block index. These checks exercise source-read bounds, not
sanitizer instrumentation of decoder memory accesses.

SDNA cases decode every structure in the generated empty scene and both real
corpus files, checking member ranges, TLEN totals and name lookup. Synthetic
cases cover both pointer widths and byte orders, multidimensional arrays,
pointer arrays, function pointers, empty structures, all short payload
prefixes, invalid sections/counts/indices/names, duplicates, size mismatches,
array overflow and trailing bytes. See the
[schema boundary](../design/BLEND_CONTRACT.md#71-schema-decoding-boundary)
for payload ownership and diagnostic offsets.

Raw datablock cases resolve every nonzero reference-target address and list
every ID block's SDNA type and stored name in the same three real files.
Synthetic cases cover null/unresolved pointers, duplicate keys, excluded
metadata collisions, both pointer widths and byte orders, non-leading
embedded ID members, output ownership, raw name bytes and invalid ID ranges,
indices, counts, members and terminators. Real screen blocks verify that `SN`
block codes and `SR` name prefixes remain distinct. Every real-file ID is also
checked with SDNA indices at the first out-of-range value and the signed and
unsigned 32-bit maxima, requiring `BLEND_DNA_INDEX` with that block's context.
See the
[raw ID boundary](../design/BLEND_CONTRACT.md#82-raw-datablock-boundary).
These raw-ID checks do not traverse pointer graphs; the importer remains
header-only.

Borrowed SDNA value tests select unaligned block elements, embedded members,
multidimensional array elements and pointer arrays in all four synthetic
pointer-width/byte-order layouts. They cover signed minima, unsigned maxima,
float/double values, nonfinite source values, wrong scalar types/shapes and
exact fatal offsets/indices for malformed ranges, counts and short spans.
Both real corpus files' `FileGlobal.curscene` pointers resolve to `Scene`
records with the expected embedded ID names. The library-written `empty.blend`
instead stores a null current Scene pointer, which is preserved without an
inferred fallback. All three files' Scene unit scales are read through SDNA;
this does not prove multi-scale physical equivalence or populate a Scene IR.
See the [value boundary](../design/BLEND_CONTRACT.md#83-borrowed-sdna-value-boundary)
for borrowing and accepted scalar types. No Collection/list traversal or
scene-selection policy is exercised.
The plugin pyramid below does not run these reader CTests.

## Scene IR without OpenUSD

```powershell
ost library build libs/blendScene --target cy2026 --profile usd
ost library test libs/blendScene --target cy2026 --profile usd
```

The standalone library exports `blendScene::blendScene` and installs its
headers and CMake package. It has no link dependencies. The reader-only root
build also builds and tests it before any OpenUSD resolution.

`blendScene.ir` checks owning empty/parented/shared-mesh records, identity
defaults, source metadata, `(x, y, z) -> (x, z, -y)`, asymmetric world-matrix
conjugation, parent-child composition and preserved right-handed winding and
direction lengths. Unit regressions cover scales `1`, `0.01`, `0.001` and
`10`, equivalent synthetic one-meter cubes, translation-only matrix scaling,
parent-child composition, invalid scale/distance/affine inputs and conversion
overflow. They do not prove Blender-written unit-field semantics; the
[unit policy](../design/STAGE_CONTRACT.md#61-scene-units) defines that separate
fixture requirement. Four boundary CTests scan forbidden includes, regenerate
CMake File API metadata, inspect the generated link line and reject forbidden
dependencies. They reuse the reader's link-test helpers without changing its
default policy. Metadata setup runs serially to avoid simultaneous root
reconfiguration when CTest uses parallel workers.

These are synthetic IR tests, not native scene decoding or USD mesh tests.
The [IR contract](../design/DESIGN_POLICY.md#521-scene-ir-foundation)
defines matrix storage and ownership; supported scope is in the
[capability matrix](../reference/CAPABILITY_MATRIX.md#5-scene-ir).

## Inspection tool

For a complete inspection workflow, output interpretation and deliberately
failing argument examples, see [Inspecting a .blend file](inspecting.md).

The reader preset also builds `tools/blendInspect/bin/blend_inspect` (`.exe`
on Windows), without finding OpenUSD. `blendInspect.cli` checks summaries and
all three detail modes against the complete Blender-written fixtures, errors
and recoverable diagnostics, malformed DNA1 and space-containing UTF-8 paths.
Header-only synthetic fixtures fail because inspection requires a full
container with one DNA1 schema.

OpenStrata builds the same target through the root workspace:

```powershell
ost build --target cy2026 --profile usd
ost test --target cy2026 --profile usd
tools/blendInspect/bin/blend_inspect.exe plugins/usdBlendFileFormat/tests/corpus/blender-4.5.13/Untitled.blend
tools/blendInspect/bin/blend_inspect.exe plugins/usdBlendFileFormat/tests/fixtures/empty.blend --blocks --dna --objects
tools/blendInspect/bin/blend_inspect.exe plugins/usdBlendFileFormat/tests/corpus/blender-5.2.2/Untitled.blend --objects --max-input-bytes 67108864 --max-output-bytes 67108864 --max-expansion-ratio 2048 --max-window-log 23
```

On Linux, omit `.exe`. Uncompressed inputs need no limit options; compressed
inputs require all four. The numbers above are test budgets, not production
defaults or the resolution of BLEND-O5. Names keep their stored prefixes;
non-ASCII and control bytes are escaped. `--objects` does not decode
transforms, geometry or scene membership. See the
[CLI contract](../design/DESIGN_POLICY.md#54-blend_inspect--the-tool) for
output and exit-status semantics.

`tools/blendInspect` also configures standalone with an installed `blendFile`
prefix on `CMAKE_PREFIX_PATH`. Both modes stage the executable in the tool
member's `bin/`; `cmake --install` installs it into the prefix's binary
directory. The tool descriptor and workspace release membership allow:

```powershell
ost plugin test --workspace --graph-only
ost plugin package --workspace --product --target cy2026 --profile usd
```

Packaging requires built outputs. It writes the tool archive beneath
`tools/blendInspect/dist/` and the aggregate beneath root `dist/`, without
publishing either. The aggregate carries both `blend_inspect` and
`usdBlendFileFormat`.

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
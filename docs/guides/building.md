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
headers and CMake package. Its descriptor declares the `blendFile` prerequisite,
which `ost` builds and installs before resolving the standalone package.
The composed root reuses the in-tree reader target, including in the reader-only
build before any OpenUSD resolution. Plain standalone CMake requires installed
`blendScene` and `blendFile` package prefixes when consuming the exported target.

`blendScene.ir` checks owning empty/parented/shared-mesh records, identity
defaults, source metadata, `(x, y, z) -> (x, z, -y)`, asymmetric world-matrix
conjugation, parent-child composition and preserved right-handed winding and
direction lengths. Unit regressions cover scales `1`, `0.01`, `0.001` and
`10`, equivalent synthetic one-meter cubes, translation-only matrix scaling,
parent-child composition, invalid scale/distance/affine inputs and conversion
overflow. `ParentRelativeTransform` regressions construct locals from normalized
worlds, covering roots, shear, pivot swaps, nonuniform/negative/zero scales,
the same four unit scales, extreme finite scales, singular/numerically singular
parents, nonfinite/projective inputs and arithmetic overflow. They do not prove Blender-written unit-field semantics; the
[unit policy](../design/STAGE_CONTRACT.md#61-scene-units) defines that separate
fixture requirement. Four boundary CTests scan forbidden includes, regenerate
CMake File API metadata, inspect the generated link line and reject forbidden
dependencies. They reuse the reader's link-test helpers without changing its
default policy. Metadata setup runs serially to avoid simultaneous root
reconfiguration when CTest uses parallel workers.

The same `blendScene.ir` test selects the saved active Scene through
`FileGlobal.curscene` in both normal-save corpus files and checks owning source
name/version/unit metadata. The Scene-only `empty.blend` must fail without an
implicit fallback. Synthetic two-Scene layouts exercise both pointer widths
and byte orders, non-first selection, block reordering, output ownership,
missing/duplicate/wrong globals, null/unresolved/interior/wrong-type references,
duplicate saved addresses, linked IDs, invalid names and unit scales, and
propagated SDNA failures. Failures require exact fatal codes and source context.
The scene link gate allows the reader but rejects all other non-system libraries;
the reader's default forbidden-edge policy remains unchanged.

Selection follows the
[saved-scene boundary](../design/DESIGN_POLICY.md#522-saved-scene-selection-boundary),
not a Collection walk, object/mesh decoder or populated Scene IR. Neither these
selection tests nor the synthetic IR arithmetic tests author USD geometry.

The same test separately exercises `SelectSceneObjects` under the
[Collection membership boundary](../design/DESIGN_POLICY.md#523-saved-collection-membership-boundary).
Both normal-save corpus files select Camera/Cube/Light through their master
Collection; the Scene-only library still fails without a fallback. Four
synthetic pointer-width/byte-order layouts cover nested and empty Collections,
shared child references, Object deduplication, block reordering, owning names,
linked/unresolved/interior/wrong-type/count references, malformed ListBase
endpoints/backlinks/last/shared nodes, list and Collection cycles, reader-error
propagation and exact visit/depth limits. A 256-child chain exercises the
explicit DFS stack at the exact depth and one smaller. Every failure requires
the exact fatal, non-recoverable code and payload offset/block index. This
selects saved membership, not instance references, render or
view-layer visibility, object values, transforms, meshes or a populated IR.

The same selection also checks the
[saved parent-reference boundary](../design/DESIGN_POLICY.md#524-saved-object-parent-reference-boundary).
Both corpus files have null Object parents; every Object is tested with
self/interior/absent parent-pointer mutations and exact fatal context.
Four synthetic layouts cover selected and unselected parents, shared ancestors,
reordered block indices, wrong targets/member shapes, linked/invalid parent IDs,
self/ancestor cycles and 256-parent chains. Exactly sufficient visit/depth
budgets succeed and one-smaller budgets fail. Parent-only Objects are validated
without adding them to membership; returned parent indices address the caller's
blocks, not its selected Object vector or IR. Nontrivial parenting remains
synthetic evidence, not Blender-written transform or hierarchy equivalence.

The same test exercises the
[saved data-reference boundary](../design/DESIGN_POLICY.md#525-saved-object-data-reference-boundary).
Both normal-save corpus files resolve Camera/Cube/Light data to Camera/Mesh/Lamp
IDs with `CA`/`ME`/`LA` codes. The 4.5 `void *data` and 5.2 `ID *data`
declarations are checked through SDNA. Every corpus Object has null/interior/
absent data-pointer mutations. Four synthetic layouts cover shared and
parent-only data, reordered indices, malformed pointer/ID shapes, invalid
targets, linked data, exact visit budgets and Object/data visit deduplication.
Returned data indices refer to the caller's blocks. This is generic ID-edge
validation: null is allowed without checking `Object.type`, data-internal
references are not followed and no Mesh values or Scene IR are populated.

The opt-in `SelectSceneObjectValues` checks the
[saved Object value boundary](../design/DESIGN_POLICY.md#526-saved-object-value-boundary)
without changing generic selection. Four synthetic layouts check type-to-data
code/SDNA mappings, scalar shapes, parent-only values, owning results and
instance targets under bounded recursive graph validation. Both normal-save
corpus files exercise Camera/Cube/Light values, the stored `restrictflag`
short/int difference and `dup_group` pointer/flag mutations. Synthetic layouts
cover nested missing/linked/invalid instance targets, recursive cycles and
256-child instance subgraphs at exact and one-smaller visit/depth limits.
Only the Object render bit is interpreted; recursively reached Objects are
validated but do not expand selected-scene membership.
Transform construction, geometry and USD authoring remain separate boundaries.

The same test exercises library-only `DecodeScene` under the
[native decoding boundary](../design/DESIGN_POLICY.md#527-native-scene-decoding-boundary).
Four synthetic layouts cover owning empty/parented IR, selected and parent-only
parent mapping, render visibility, block reordering, Euler/Quaternion/Axis-Angle and delta
channels, nonuniform/negative scales, column-first parent inverses and
single-pass normalization at four unit scales. Deep 256-parent chains succeed
at exact visit/depth budgets and fail at one smaller; source-matrix and
meter-conversion overflow return fatal context. Unsupported kinds, Image
Empty data, enabled instances, unknown rotation/non-ordinary parenting modes, malformed
storage and nonfinite inputs fail without a partial Scene. Animation and
constraint presence emit recoverable source-only diagnostics. Both real corpus
SDNA layouts are exercised through in-memory Empty-kind/data mutations;
the original files retain unsupported Camera/Light objects, and the
Scene-only library retains its missing-active-Scene error.

`blendScene.transforms` reads separate unmodified Blender-written 4.5.13 and
5.2.2 fixtures and their saved matrix oracles. It compares 27 Empty objects per
file, covering all six Euler orders, nonunit and zero Quaternion/Axis-Angle
channels, applicable delta transforms, nonuniform/negative/zero scale,
render visibility, parent inverses, constructed parent-relative local matrices
and reconstructed three-level parent/local composition
after one meter/basis conversion. Repeated reads with reversed block records
must produce identical object order, parent indices, world and constructed
local matrices.
Native/oracle matrix components use `2e-5 * (1 + abs(expected))` tolerance.
Only oracle-local homogeneous-row rounding is accepted within `1e-6`; the
decoder's own affine validation remains exact. Neither test authors USD or
closes STAGE-O1's multi-scale evidence. Fixture provenance and regeneration
are in the [transform fixture README](../../tests/fixtures/native-transforms/README.md).

The same `blendScene.ir` executable includes `MeshTests.cpp`, exercising the
[native Mesh storage boundary](../design/DESIGN_POLICY.md#528-native-mesh-storage-boundary).
The real 4.5.13 CustomData and 5.2.2 AttributeArray Cube Mesh payloads retain
their original bytes; non-Mesh Objects are mutated to Empty only in memory.
Tests pin all source points, face indices and 24 outward corner normals,
named indexed `UVMap` coordinates and repeated-read equality. Both storage
forms also run across four synthetic layouts, covering mixed Mesh/Empty
objects, shared Mesh indices, two indexed UV maps with a non-first render map,
block reordering, ownership and four unit scales without scaling normals/UVs.
Empty, malformed, nonfinite, invalid-index and unsupported custom/constant
storage cases require exact contextual diagnostics and no partial Scene.
Modifier and shape-key presence reports source-only data without evaluation.
The root reader build and standalone OpenStrata library build both run these
tests and the existing four scene dependency-boundary gates. Blender-written Mesh transform/unit oracle fixtures
and USD geometry remain unproven; no Blender executable is required by these CTests.

`blendScene.normals` compares unchanged Blender-written 4.5.13 and 5.2.2 files
against saved point, topology and corner-normal oracles. Each version has
three single-Mesh fixtures with 15 geometry cases and 137 corners: flat,
entirely smooth point normals and mixed/sharp split fans, including unequal
corner angles, open/closed fans, concave polygons, disconnected faces,
nonmanifold edges and same-direction edge uses. Normal components compare
within absolute `2e-5`; output normals must have unit length within `1e-12`.
Repeated reads with reversed block records must produce identical arrays.
The synthetic IR tests also check angle weighting, missing sharp-face defaults,
unit-scale independence, edge storage/range/endpoint errors and cancelling
smooth normal sums. Both root and standalone Scene CTest suites include this
oracle comparison, without requiring Blender at test time.

The same normal test compares both independent two-Mesh fixtures and four
packed custom-normal fixtures per version. Shared/open/closed and mixed
packed-pair fans, automatic values, signed-short minima and 199 angle-sweep
triangles cover 1,588 custom corners at the same `2e-5` threshold; maximum
measured component error is below `6.25e-6`. `blendScene.meshBoundaries`
retains exact saved-short/RNA comparisons and global `BuildPointerMap`
rejection for non-identical 5.2.2 Attribute/AttributeArray collisions.
Scene selection/decoding resolve only validated Mesh-owned occurrences.
Wrong/same/non-Mesh owners, duplicate IDs, unreferenced targets, unrelated
SDNA types and legacy-header mutations remain contextual fatal errors.
The reader's global uniqueness contract and the header-only importer remain
unchanged.

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

### Saved transform oracles

Run with each pinned Blender installation (4.5.13 or 5.2.2). The default output
is `tests/fixtures/native-transforms/blender-<version>/transforms.blend` and its
adjacent `transforms.oracle.txt`:

```powershell
$blender = Join-Path $env:ProgramFiles 'Blender Foundation\Blender 4.5\blender.exe'
& $blender --background --factory-startup --disable-autoexec --python-exit-code 1 --python .\tests\fixtures\generate_transforms.py
& $blender --background --factory-startup --disable-autoexec --python-exit-code 1 --python .\tests\fixtures\generate_transforms.py -- --check
& $blender --background --factory-startup --disable-autoexec --python-exit-code 1 --python .\tests\fixtures\test_generate_transforms.py

$blender = Join-Path $env:ProgramFiles 'Blender Foundation\Blender 5.2\blender.exe'
& $blender --background --factory-startup --disable-autoexec --python-exit-code 1 --python .\tests\fixtures\generate_transforms.py
& $blender --background --factory-startup --disable-autoexec --python-exit-code 1 --python .\tests\fixtures\generate_transforms.py -- --check
& $blender --background --factory-startup --disable-autoexec --python-exit-code 1 --python .\tests\fixtures\test_generate_transforms.py
```

The full-file writer saves an active Scene, unlike the Scene-only library
fixture. File-browser directories are normalized to `//` before saving to
avoid machine-local UI paths. `--check` regenerates only into a temporary
directory, compares the semantic oracle, then opens and checks the stored
fixture without rewriting it. `--output <path.blend>` redirects either mode.
The regression suite checks cross-process/path oracle reproduction,
non-destructive checks, absence of the user's home path, modified-oracle
rejection and validation of changed saved transforms. Full-file saved addresses
and UI state are not byte-reproducible; no `--check-bytes` claim is made.

### Saved normal oracles

Run with each pinned Blender installation (4.5.13 or 5.2.2). The default
output directory holds `smooth.blend`, `flat.blend`, `split.blend`,
`custom.blend`, `custom_fans.blend`, `custom_split_fans.blend`,
`custom_angles.blend`, `multi.blend` and adjacent `.oracle.txt` files.
Provenance, cases, comparison thresholds and ownership/reconstruction evidence are in the
[normal fixture record](../../tests/fixtures/native-normals/README.md).

```powershell
$blender = Join-Path $env:ProgramFiles 'Blender Foundation\Blender 4.5\blender.exe'
& $blender --background --factory-startup --disable-autoexec --python-exit-code 1 --python .\tests\fixtures\generate_normals.py
& $blender --background --factory-startup --disable-autoexec --python-exit-code 1 --python .\tests\fixtures\generate_normals.py -- --check
& $blender --background --factory-startup --disable-autoexec --python-exit-code 1 --python .\tests\fixtures\test_generate_normals.py

$blender = Join-Path $env:ProgramFiles 'Blender Foundation\Blender 5.2\blender.exe'
& $blender --background --factory-startup --disable-autoexec --python-exit-code 1 --python .\tests\fixtures\generate_normals.py
& $blender --background --factory-startup --disable-autoexec --python-exit-code 1 --python .\tests\fixtures\generate_normals.py -- --check
& $blender --background --factory-startup --disable-autoexec --python-exit-code 1 --python .\tests\fixtures\test_generate_normals.py
```

To check only the additional custom-space fixtures without rewriting any
fixtures, use `--check --groups custom_fans custom_split_fans custom_angles`:

```powershell
$blender = Join-Path $env:ProgramFiles 'Blender Foundation\Blender 4.5\blender.exe'
& $blender --background --factory-startup --disable-autoexec --python-exit-code 1 --python .\tests\fixtures\generate_normals.py -- --check --groups custom_fans custom_split_fans custom_angles
$blender = Join-Path $env:ProgramFiles 'Blender Foundation\Blender 5.2\blender.exe'
& $blender --background --factory-startup --disable-autoexec --python-exit-code 1 --python .\tests\fixtures\generate_normals.py -- --check --groups custom_fans custom_split_fans custom_angles
```

The generator constructs source geometry without modifiers. All `custom*`
groups record Blender's resulting corner normals and packed short pairs.
The fan fixtures exercise shared spaces and integer averaging; the angle
fixture pins the observed reference-angle approximation independently of
native decoder math. `multi` creates separate Mesh datablocks, not copies or shared
Object data, with uniform scaling and translation of the second Mesh's points.
`--check` regenerates into temporary files, compares the semantic oracles,
then reopens and verifies each stored fixture without rewriting any fixture
or oracle. `--output <directory>` redirects all selected files in either mode;
`--groups` applies equally to generation and checks.
The regression suite checks every oracle's cross-process/path reproduction,
non-destructive checks of all files, absence of Windows absolute UI/home paths,
modified-oracle rejection, unchanged unselected files, validation of changed
saved smoothing flags/custom normals/second-Mesh geometry, and same-shape,
non-identical 5.2.2 address collisions across independent processes.
Saved addresses and UI state are not byte-reproducible.
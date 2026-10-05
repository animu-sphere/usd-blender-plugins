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
	manifest's installed `blendFile` and `blendScene` dependencies.

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
ten stage-contract tests plus the standalone `usdBlend.authoring`,
`usdBlend.units`, `usdBlend.importer` and `usdBlend.instances` CTests, and
uploads their reports and logs. These additional
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
for values, measurement semantics and corpus limitations. The separately
accepted standard policy and large generated-input evidence are in the
[blend contract](../design/BLEND_CONTRACT.md#42-standard-full-stream-limit-policy)
and [dated report](../reports/2026-10-05-compression-policy.md).

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
These raw-ID checks do not traverse pointer graphs; the importer composes
native graph decoding separately.

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
meter-conversion overflow return fatal context. Unmapped kinds,
enabled instances, unknown rotation/non-ordinary parenting modes, malformed
storage and nonfinite inputs fail without a partial Scene. Animation and
constraint presence emit recoverable source-only diagnostics. Both real corpus
SDNA layouts are exercised through in-memory Empty-kind/data mutations;
the unchanged corpus Scenes now decode Camera/Light as diagnostic-bearing
Empty Objects alongside the Cube Mesh. All mapped non-Mesh kinds and Image
Empty have four-layout fallback coverage, without relaxing fatal reference,
linked-ID, transform or instance validation. The
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
provides the separate [multi-scale evidence](../../tests/fixtures/native-units/README.md).
Fixture provenance and regeneration
are in the [transform fixture README](../../tests/fixtures/native-transforms/README.md).

`blendScene.naming` exercises the
[native Object naming boundary](../design/NAMING_POLICY.md#41-native-object-naming-boundary):
ASCII sanitization, preserved literal underscores, unsigned UTF-8 source-byte
ordering, natural suffix collisions and fixed `mesh` child reservations.
All 40,320 permutations of an eight-Object IR, with parent indices remapped,
must assign the same identifiers to the same source Objects. UTF-8 display
tests pin valid scalar boundaries, maximal ill-formed-subpart replacement,
one recoverable warning per affected name, duplicate sibling rejection and
invalid immediate IR references.

`blendScene.ir` connects naming to native decoding across all four synthetic
layouts, requiring unchanged raw names and exact Object context for warnings
and fatal duplicate-name failures. Both Mesh storage families prove fixed-child
reservation without changing shared geometry. `blendScene.transforms` also
requires unchanged identifiers when the unmodified Blender-written fixtures'
block records are reversed. These tests run in both root and standalone Scene
builds alongside the existing dependency gates. They do not author USD names,
or test registered-plugin reads; separate
[multi-scale fixtures](../../tests/fixtures/native-units/README.md) exercise
the frozen policy through native decoding and USD name authoring.

The same `blendScene.ir` executable includes `MeshTests.cpp`, exercising the
[native Mesh storage boundary](../design/DESIGN_POLICY.md#528-native-mesh-storage-boundary).
The real 4.5.13 CustomData and 5.2.2 AttributeArray Cube Mesh payloads retain
their original bytes; non-Mesh Objects are mutated to Empty only in memory.
Tests pin all source points, face indices and 24 outward corner normals,
named indexed `UVMap` coordinates and repeated-read equality. Both storage
forms also run across four synthetic layouts, covering mixed Mesh/Empty
objects, shared Mesh indices, two indexed UV maps with a non-first render map,
block reordering, ownership and four unit scales without scaling normals/UVs.
Constant AttributeArrays and AttributeSingles run raw/structured values across
all four layouts, including empty domains, face/edge booleans, UV indexing and
packed normals. Invalid sizes, single flags, lengths and references retain
exact fatal context. Dense zero-domain AttributeArrays do not follow their
unused data keys, including nonzero keys without serialized DATA; nonempty
and constant arrays retain strict reference/payload validation.
Empty, malformed, nonfinite, invalid-index and unsupported
named-normal/storage cases require no partial Scene.
Legacy type-16 `MLoopUV` layers also run across all four synthetic layouts,
including two maps and mixed float2/MLoopUV maps, SDNA-defined offsets/strides,
ignored record flags, indexed signed-zero/out-of-range coordinates, render
selectors, unit independence, empty domains and owning/repeated/reordered
reads. Invalid SDNA/value shapes, counts, lengths, pointers, layer domains,
names and flags require contextual fatal failures without a partial Scene.
The unchanged 3.3.21 Mesh-domain fixture additionally compares Blender-written
MLoopUV maps against its saved oracle; the two modern corpus files retain
their float2/AttributeArray coverage.
Legacy fixed `MVert`/`MLoop`/`MPoly` arrays run across the same four layouts,
with SDNA-defined offsets and strides, flat/smooth/mixed polygon flags,
`MEdge` sharp flags, split fans, packed automatic normals, float2/MLoopUV maps,
loose points, sharing, ownership, repeated/reversed reads and four unit scales.
Malformed references, record lengths/counts/types, member shapes, nonfinite
positions, signed/unsigned indices and noncontiguous/invalid polygon ranges retain
exact fatal context. Saved legacy pointers do not repair partial or invalid
modern core attributes/offsets. Separate 3.3 cases cover absent modern members,
strict modern-member requirements, CustomData geometry aliases and the older
default point-normal policy. The unchanged 3.3.21 Mesh-domain fixture now
provides Blender-written legacy geometry/normal/transform/UV evidence.
Modifier and shape-key presence reports source-only data without evaluation.
The root reader build and standalone OpenStrata library build both run these
tests and the existing four scene dependency-boundary gates. No Blender
executable is required by these CTests.

`blendScene.sceneFixture` reads unchanged Blender-written 4.5.13/5.2.2
[integrated Scene fixtures](../../tests/fixtures/native-scene/README.md).
It combines selected membership, Mesh/Empty hierarchy, three users of one
Mesh, an unselected Mesh parent, render visibility and fixed-child naming
with world/local matrices, points, topology, mixed normals and two indexed UV
maps. The owning IR is compared after reader inputs are released. Repeated
loads and reversed block records must preserve all metadata, discovery order,
indices, matrices and Mesh/UV arrays exactly. Root and standalone Scene
builds include this CTest without introducing Blender or OpenUSD dependencies.
The fixture uses one source scale; multi-scale unit and USD/importer
integration remain separate evidence requirements.

`blendScene.meshFixture` uses the same executable to compare the saved
[Mesh-domain oracles](../../tests/fixtures/native-mesh/README.md): empty and
loose-point Meshes with empty UV domains, UV-free polygons, corner seams,
signed-zero/out-of-range coordinates, constant UV coordinates and distinct
editing/render maps. Exact first-occurrence UV values and indices, contextual
empty warnings and repeated/reversed owning reads are checked in both build
modes. Registered-plugin tests compare the same oracles, extent, metadata-only
hierarchy and reference composition. The 5.2.2 file reproduces unused nonzero
data keys in zero-domain dense AttributeArrays.

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
measured component error is below `3e-7` after sharing modern corner-angle
weights. `blendScene.meshBoundaries`
retains exact saved-short/RNA comparisons and global `BuildPointerMap`
rejection for non-identical 5.2.2 Attribute/AttributeArray collisions.
Scene selection/decoding resolve only validated Mesh-owned occurrences.
Wrong/same/non-Mesh owners, duplicate IDs, unreferenced targets, unrelated
SDNA types and legacy-header mutations remain contextual fatal errors.
Both `constant.blend` files compare two independent flat Meshes with saved
normal oracles. The 5.2.2 fixture stores `sharp_face` as `AttributeSingle`,
one true byte for two faces. In-memory Single-address collisions exercise
the same scoped ownership checks, including wrong storage discriminators.
Single-flag AttributeArrays have synthetic evidence only.
The reader's global uniqueness contract remains unchanged. The importer
uses these native Mesh ownership checks rather than a global-only pointer map.

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
implicit defaults. The accepted
[standard policy](../design/BLEND_CONTRACT.md#42-standard-full-stream-limit-policy)
is supplied explicitly. Names keep their stored prefixes;
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

The cube's LF golden and clean packaged discovery/read were also checked on
Windows on 2026-10-04:

```powershell
ost plugin package plugins\usdBlendFileFormat --target cy2026 --profile usd
ost plugin test plugins\usdBlendFileFormat --target cy2026 --profile usd --from-package
```

The manifest declares the `blendFile` and `blendScene` edges; `ost` builds and
installs both libraries into its workspace prefix before configuring the standalone bundle.
The ten stage tests assert the registered cube, integrated Scene,
unsupported-data fallback, Mesh-domain, legacy auto-smooth and 5.2 polygon-fan
oracles, multi-scale imports, metadata-only hierarchy, contextual fatal/recoverable
diagnostics, repeat-read determinism and referenced geometry. Compressed
regressions use the unchanged Blender-written 5.2.2 corpus plus temporary
gzip/raw-Zstandard encodings of Cube, integrated Scene and fallback fixtures,
including concatenated streams and decoded-size block bounds. The importer
explicitly selects the accepted full-stream policy without changing explicit
reader/CLI limits.

Six fixtures are synthetic legacy headers; `empty.blend` is a complete
Scene-only library written by Blender 5.2.2 LTS. Header-only and Scene-only
inputs are now negative fixtures, not scaffold-stage successes. L3/L4 use
the normal-save `single_cube.blend`; L5 compares its flattened Mesh stage
against the golden. The generated source comment has its
path removed by `ost` normalization. USDA files must use LF endings.

### Scene IR USD authoring

`usdBlend.authoring` exercises the bundle's internal authoring translation unit
and byte-to-Scene input composition. Synthetic IR pins parent-relative
matrix transposition, visibility, duplicated Meshes, points/topology/normals,
extent, indexed UV schema and `st` reservation, provenance, errors and
determinism. Blender-written 4.5.13/5.2.2 transform fixtures compare all 27
Objects' authored world/local matrices against their saved oracle at `2e-5`;
independent two-Mesh fixtures retain native geometry. Repeated and reversed
native block reads author identical text. Float-range checks accept the exact
maximum and reject the next larger double. Singular roots without children
and zero-scale leaves succeed; a singular authored parent fails explicitly.

`usdBlend.units` uses the same internal authoring executable with eight
Blender-written multi-scale fixtures. It checks native and authored values
against independent saved-value/world-vertex oracles and compares all four
scales across both Blender versions. Physical one-meter geometry, active-Scene
scale selection, reflected/sheared parenting, unchanged normals/UVs, fixed USD
units and explicit ASCII naming expectations are pinned. Repeated and reversed
block reads author identical text. Saved cached handedness bit 2 does not
reapply signed transforms; other flag bits remain unsupported.
The [fixture record](../../tests/fixtures/native-units/README.md#oracle-and-checks)
owns exact tolerances and provenance. Both CTests also compare the importer's
`ReadScene` composition to independently composed native authoring and check
metadata-only output without geometry attributes.

After the root plain-CMake build below and the standalone bundle build above,
the following validation commands were exercised on Windows on 2026-10-04:

```powershell
ost plugin run plugins\usdBlendFileFormat --target cy2026 --profile usd -- ctest --test-dir build\usd-vs18 -C Release --output-on-failure --no-tests=error
ost library test libs\blendScene --target cy2026 --profile usd
ost plugin run plugins\usdBlendFileFormat --target cy2026 --profile usd -- ctest --test-dir plugins\usdBlendFileFormat\build\cy2026-windows-x86_64-py313-usd -C Release --output-on-failure --no-tests=error -R '^usdBlend\.(authoring|units)$'
```

The first command covers root CTests, including native/reader dependency
gates and the inspection CLI. The others cover the standalone Scene suite
and both standalone authoring/unit tests, with the same runtime
activation as the plugin.
Use the corresponding build directories on other hosts. The companion CI
discovers its single standalone CTest directory rather than duplicating the
runtime target triplet; root CI includes the new CTest automatically.

### Compressed importer

`usdBlend.importer` compares the unchanged Blender-written 5.2.2 Zstandard
corpus against importing its decoded bytes. Full/metadata layer text and
every native warning field must agree. Exact input/output/ratio/window budgets
must succeed; reducing any one below the corpus minimum must preserve its
fatal `BLEND_COMPRESSION_*` code. A sparse source checks the default input
limit without allocating a large file; a failing source checks payload read
errors. Small compressed budgets do not cap uncompressed Cube importing.
Registered-plugin tests additionally check gzip CRC failure, truncation,
trailing garbage, default ratio/window failures, decoded DNA offsets,
metadata, repeats and references.

The following focused commands were exercised on Windows on 2026-10-05,
after building both modes:

```powershell
ost plugin run plugins\usdBlendFileFormat --target cy2026 --profile usd -- ctest --test-dir build\usd-vs18 -C Release --output-on-failure --no-tests=error -R '^usdBlend\.(authoring|units|importer)$'
ost plugin run plugins\usdBlendFileFormat --target cy2026 --profile usd -- ctest --test-dir plugins\usdBlendFileFormat\build\cy2026-windows-x86_64-py313-usd -C Release --output-on-failure --no-tests=error -R '^usdBlend\.(authoring|units|importer)$'
ost plugin run plugins\usdBlendFileFormat --target cy2026 --profile usd --no-inject --plugin-path "$PWD\plugins\usdBlendFileFormat" -- python plugins\usdBlendFileFormat\tests\test_stage.py
ost plugin run plugins\usdBlendFileFormat --target cy2026 --profile usd -- python plugins\usdBlendFileFormat\tests\test_stage.py
```

These are bounded composition regressions, not new Blender data/version
compatibility or old Blender-written gzip evidence. Hosted Windows/Linux
coverage is wired in the existing stage-contract workflow; the dated local
run does not claim hosted execution results.

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

To test the registered plain-CMake plugin rather than OpenStrata's staged
standalone binary, the following invocation was verified on Windows:

```powershell
ost plugin run plugins\usdBlendFileFormat --target cy2026 --profile usd --no-inject --plugin-path "$PWD\plugins\usdBlendFileFormat" -- python plugins\usdBlendFileFormat\tests\test_stage.py
```

This builds the reader first, resolves OpenUSD once through
`find_package(pxr CONFIG REQUIRED)` without a version constraint, then builds
the plugin. CTest runs the root build's registered tests.

## Fixture reproducibility

New compatibility fixtures follow the
[Blender 5.x version policy](../design/BLEND_CONTRACT.md#9-version-support).
The older-version commands below remain valid for reproducing and checking
retained regression evidence; they do not imply a current support guarantee
or require expanding that coverage before Phase 8.

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

### Integrated Scene oracles

Run with each pinned Blender installation (4.5.13 or 5.2.2). The default
output is `tests/fixtures/native-scene/blender-<version>/scene.blend` and
adjacent `scene.oracle.txt`. Cases, provenance and comparison thresholds
are in the [Scene fixture record](../../tests/fixtures/native-scene/README.md).

```powershell
$blender = Join-Path $env:ProgramFiles 'Blender Foundation\Blender 4.5\blender.exe'
& $blender --background --factory-startup --disable-autoexec --python-exit-code 1 --python .\tests\fixtures\generate_scene.py
& $blender --background --factory-startup --disable-autoexec --python-exit-code 1 --python .\tests\fixtures\generate_scene.py -- --check
& $blender --background --factory-startup --disable-autoexec --python-exit-code 1 --python .\tests\fixtures\test_generate_scene.py

$blender = Join-Path $env:ProgramFiles 'Blender Foundation\Blender 5.2\blender.exe'
& $blender --background --factory-startup --disable-autoexec --python-exit-code 1 --python .\tests\fixtures\generate_scene.py
& $blender --background --factory-startup --disable-autoexec --python-exit-code 1 --python .\tests\fixtures\generate_scene.py -- --check
& $blender --background --factory-startup --disable-autoexec --python-exit-code 1 --python .\tests\fixtures\test_generate_scene.py
```

`--check` regenerates only into a temporary directory, compares semantic
oracles, then validates the saved file without rewriting either committed
file. `--output <path.blend>` redirects either mode. Regression checks cover
cross-process/path oracle reproduction, non-destructive checks and failures,
modified-oracle rejection, home-path exclusion, and saved transform,
Mesh-sharing and UV mutations. Full-file bytes are not reproducible; there
is no `--check-bytes` claim. The native `blendScene.sceneFixture` regression
runs with the regular root and standalone Scene CTests above. Registered-plugin
stage tests now compare these same saved oracles against the authored stage.

### Unsupported-data fallback oracle

The pinned Blender 5.2.2 generator extends the integrated Scene with
Camera/Light/text/Image Empty data and a parent-only Camera. Provenance,
scope and thresholds are in the
[fixture record](../../tests/fixtures/native-scene/README.md#unsupported-data-fallback-oracle).

```powershell
$blender = Join-Path $env:ProgramFiles 'Blender Foundation\Blender 5.2\blender.exe'
& $blender --background --factory-startup --disable-autoexec --python-exit-code 1 --python .\tests\fixtures\generate_fallbacks.py
& $blender --background --factory-startup --disable-autoexec --python-exit-code 1 --python .\tests\fixtures\generate_fallbacks.py -- --check
& $blender --background --factory-startup --disable-autoexec --python-exit-code 1 --python .\tests\fixtures\test_generate_fallbacks.py
```

`--check` regenerates into a temporary directory, compares the semantic
oracle, then reopens and validates the saved source kinds/data and matrices
without rewriting either committed file. `--output <path.blend>` redirects
generation or checking. Generator regressions cover independent-process/path
reproduction, home-path exclusion, non-destructive checks/failures, modified
oracles, removed Image data and changed Camera transforms.
`blendScene.objectFallbacks` and `usdBlend.authoring` run in both root and
standalone builds. The registered-plugin oracle, diagnostic, repeat/metadata
and reference tests use the same saved fixture. No schema for unsupported
data, instance expansion or older-version support extension is introduced.

### Source-only evaluation oracle

The pinned Blender 5.2.2 generator extends the integrated Scene with location
animation, active Copy Location constraints, subdivision and nonzero shape
keys, including combined dependencies and a parent-only Mesh. Its oracle
uses source-channel matrices and original Mesh data, not evaluated results.
Provenance, source/evaluated distinction and comparison thresholds are in the
[fixture record](../../tests/fixtures/native-scene/README.md#source-only-evaluation-oracle).

The following commands were exercised on Windows on 2026-10-05:

```powershell
$blender = Join-Path $env:ProgramFiles 'Blender Foundation\Blender 5.2\blender.exe'
& $blender --background --factory-startup --disable-autoexec --python-exit-code 1 --python .\tests\fixtures\generate_evaluation.py
& $blender --background --factory-startup --disable-autoexec --python-exit-code 1 --python .\tests\fixtures\generate_evaluation.py -- --check
& $blender --background --factory-startup --disable-autoexec --python-exit-code 1 --python .\tests\fixtures\test_generate_evaluation.py
```

`--check` regenerates only in a temporary directory, compares source oracles,
then reopens and validates saved dependency settings and source values without
rewriting the fixture or oracle. `--output <path.blend>` redirects either mode.
Generator regressions cover cross-process/path reproduction, non-destructive
checks/failures, modified oracles and mutations of each dependency kind,
parent-only evaluation data and source transforms.

`blendScene.sourceEvaluation` runs with root and standalone Scene CTests;
`usdBlend.authoring` runs with both root and standalone plugin builds.
The existing registered-plugin selectors include source-only oracle,
full/metadata diagnostics, repeated reads and references:

```powershell
ost plugin run plugins\usdBlendFileFormat --target cy2026 --profile usd -- python plugins\usdBlendFileFormat\tests\test_stage.py StageContractTests.test_integrated_scene_oracles StageContractTests.test_recoverable_diagnostics StageContractTests.test_repeat_read_and_metadata StageContractTests.test_contract_survives_reference
```

For the root-built plugin use the same command with
`--no-inject --plugin-path "$PWD\plugins\usdBlendFileFormat"` before `--`.
These checks pin four Object warnings and one shared-Mesh warning with exact
source names/byte/block context, unchanged source geometry/transforms and
absence of time samples. No dependency-graph or animation evaluation is added.

### Recursive Collection-instance oracle

The pinned Blender 5.2.2 generator saves shared and multi-level instance
targets, instance-only Objects and parent-only inactive references. Graph
records, provenance and exact traversal thresholds are in the
[fixture record](../../tests/fixtures/native-scene/README.md#recursive-collection-instance-graph-oracle).
Generation and regression commands were exercised on Windows on 2026-10-05:

```powershell
$blender = Join-Path $env:ProgramFiles 'Blender Foundation\Blender 5.2\blender.exe'
& $blender --background --factory-startup --disable-autoexec --python-exit-code 1 --python .\tests\fixtures\generate_instances.py
& $blender --background --factory-startup --disable-autoexec --python-exit-code 1 --python .\tests\fixtures\generate_instances.py -- --check
& $blender --background --factory-startup --disable-autoexec --python-exit-code 1 --python .\tests\fixtures\test_generate_instances.py

cmake --build .\build\reader-vs18 --config Release --target blendSceneInstanceTests blendSceneTests
ctest --test-dir .\build\reader-vs18 -C Release --output-on-failure --no-tests=error -R '^blendScene\.(instances|ir)$'

ost plugin build plugins\usdBlendFileFormat --target cy2026 --profile usd
ost plugin run plugins\usdBlendFileFormat --target cy2026 --profile usd -- ctest --test-dir plugins\usdBlendFileFormat\build\cy2026-windows-x86_64-py313-usd -C Release --output-on-failure --no-tests=error -R '^usdBlend\.(instances|authoring|importer)$'
ctest --test-dir libs\blendScene\build\cy2026-windows-x86_64-py313-usd -C Release --output-on-failure --no-tests=error -R '^blendScene\.(instances|ir|sceneFixture|objectFallbacks|sourceEvaluation)$'

ost plugin run plugins\usdBlendFileFormat --target cy2026 --profile usd --no-inject --plugin-path "$PWD\plugins\usdBlendFileFormat" -- ctest --test-dir build\usd-vs18 -C Release --output-on-failure --no-tests=error -R '^(blendScene\.instances|usdBlend\.(instances|authoring|importer))$'
```

The root build directories above are the configured local Visual Studio
builds described in this guide, not newly required presets. Plugin CMake
tests discover a Python interpreter; `usdBlend.instances` needs `pxr` and
plugin discovery from the activated SDK, so run it through `ost plugin run`.
Reader-only and standalone Scene tests still require neither Python nor USD.

`--output <path.blend>` redirects generator/check inputs. `--check` never
rewrites the saved fixture or graph oracle. The native case writer supplies
24 temporary variants to the registered-plugin test, including invalid nested
references, linked-ID mutations, cycles, list errors and parent-only references.
Full/metadata plain/gzip/Zstandard reads pin exact decoded-file error context,
error precedence, repeated failures and successful source-only references.
These are graph-validation regressions; instance expansion and broader
Scene/Blender-version support are not added.

### Saved Mesh-domain oracles

Run with each pinned Blender installation (4.5.13 or 5.2.2). The default
output is `tests/fixtures/native-mesh/blender-<version>/mesh.blend` and its
adjacent ASCII oracle. Cases and provenance are in the
[Mesh fixture record](../../tests/fixtures/native-mesh/README.md).

```powershell
$blender = Join-Path $env:ProgramFiles 'Blender Foundation\Blender 4.5\blender.exe'
& $blender --background --factory-startup --disable-autoexec --python-exit-code 1 --python .\tests\fixtures\generate_mesh.py
& $blender --background --factory-startup --disable-autoexec --python-exit-code 1 --python .\tests\fixtures\generate_mesh.py -- --check
& $blender --background --factory-startup --disable-autoexec --python-exit-code 1 --python .\tests\fixtures\test_generate_mesh.py

$blender = Join-Path $env:ProgramFiles 'Blender Foundation\Blender 5.2\blender.exe'
& $blender --background --factory-startup --disable-autoexec --python-exit-code 1 --python .\tests\fixtures\generate_mesh.py
& $blender --background --factory-startup --disable-autoexec --python-exit-code 1 --python .\tests\fixtures\generate_mesh.py -- --check
& $blender --background --factory-startup --disable-autoexec --python-exit-code 1 --python .\tests\fixtures\test_generate_mesh.py

ost library test libs\blendScene --target cy2026 --profile usd --filter 'blendScene\.(ir|meshFixture|sceneFixture|normals|meshBoundaries|boundary|link)'
ost plugin run plugins\usdBlendFileFormat --target cy2026 --profile usd -- python plugins\usdBlendFileFormat\tests\test_stage.py StageContractTests.test_integrated_scene_oracles StageContractTests.test_repeat_read_and_metadata StageContractTests.test_contract_survives_reference
```

`--check` is non-destructive and checks both a temporary regeneration and the
saved input. `--output <path.blend>` redirects either mode. UV comparisons
are exact, including zero signs; other numeric Scene-oracle values retain
the existing `2e-6` relative/absolute generator tolerance. Regression checks
reject saved UV-coordinate/zero-sign/render-selector/loose-point/sharing
mutations. Normal-save bytes are not reproducible; no `--check-bytes` claim
is made.

### Legacy Mesh storage evidence

Blender 3.3.21 generates and verifies the tested legacy Mesh storage and
default-normal oracle. Download the official Windows portable
archive and verify it against
[Blender's SHA-256 list](https://download.blender.org/release/Blender3.3/blender-3.3.21.sha256).
Keep the executable outside the repository; no Blender binary is bundled.
Set `$blender` to the full path of your verified portable `blender.exe`.
The following commands were exercised on Windows on 2026-10-05:

```powershell
& $blender --background --factory-startup --disable-autoexec --python-exit-code 1 --python .\tests\fixtures\generate_mesh.py
& $blender --background --factory-startup --disable-autoexec --python-exit-code 1 --python .\tests\fixtures\generate_mesh.py -- --check
& $blender --background --factory-startup --disable-autoexec --python-exit-code 1 --python .\tests\fixtures\test_generate_mesh.py

cmake --preset reader
cmake --build --preset reader
ctest --preset reader --output-on-failure
ost library build libs\blendScene --target cy2026 --profile usd
ost library test libs\blendScene --target cy2026 --profile usd --filter 'blendScene\.(legacyMeshStorage|meshBoundaries|ir|meshFixture|sceneFixture|boundary|link)'
```

The native `blendScene.legacyMeshStorage` CTest runs without Blender or OpenUSD
in both build modes. It checks unmodified raw saved values plus contextual
signed-index, unverified-version and unsupported-normal-mode mutations,
including repeated/reversed block reads. `blendScene.meshFixture` compares
the decoded legacy Scene IR to the complete saved oracle; `usdBlend.authoring`
and registered-plugin stage tests cover USD composition. Scope, default-normal
semantics, tolerances and provenance are in the
[fixture record](../../tests/fixtures/native-mesh/README.md#legacy-scene-decoding).

The following additional checks were exercised on Windows on 2026-10-05:

```powershell
ost plugin run plugins\usdBlendFileFormat --target cy2026 --profile usd -- ctest --test-dir build\usd-vs18 -C Release --output-on-failure -R '^usdBlend\.(authoring|units)$'
ost plugin run plugins\usdBlendFileFormat --target cy2026 --profile usd --no-inject --plugin-path "$PWD\plugins\usdBlendFileFormat" -- python plugins\usdBlendFileFormat\tests\test_stage.py
```

### Legacy auto-smooth oracles

Use the same verified external Blender 3.3.21 portable installation above.
The following commands were exercised on Windows on 2026-10-05:

```powershell
& $blender --background --factory-startup --disable-autoexec --python-exit-code 1 --python .\tests\fixtures\generate_normals.py
& $blender --background --factory-startup --disable-autoexec --python-exit-code 1 --python .\tests\fixtures\generate_normals.py -- --check
& $blender --background --factory-startup --disable-autoexec --python-exit-code 1 --python .\tests\fixtures\test_generate_legacy_normals.py

cmake --preset reader
cmake --build --preset reader
ctest --preset reader -R '^blendScene\.(legacyNormals|normals|legacyMeshStorage|ir|meshBoundaries|meshFixture)$'
ost library test libs\blendScene --target cy2026 --profile usd --filter 'blendScene\.(legacyNormals|normals|legacyMeshStorage|meshBoundaries|ir|meshFixture|boundary|link)'
ost plugin build plugins\usdBlendFileFormat --target cy2026 --profile usd
ost plugin run plugins\usdBlendFileFormat --target cy2026 --profile usd -- ctest --test-dir plugins\usdBlendFileFormat\build\cy2026-windows-x86_64-py313-usd -C Release --output-on-failure --no-tests=error -R '^usdBlend\.(authoring|units)$'
ost plugin run plugins\usdBlendFileFormat --target cy2026 --profile usd -- python plugins\usdBlendFileFormat\tests\test_stage.py
```

On 3.3.21 the normal generator defaults to `auto_smooth`, `auto_angle`,
`auto_zero` and `auto_boundary`, not the modern fixture groups. Their saved
0/60/90/180-degree thresholds, sharp/flat boundaries and connected-fan cases
compare to Blender's split-normal oracle without evaluation. `--check` never
rewrites committed fixtures; regressions also reject saved mode/angle/flag
mutations and changed oracles. Other Blender versions retain their existing
generation groups. Provenance, exact storage and comparison tolerances are
in the [fixture record](../../tests/fixtures/native-normals/README.md#legacy-auto-smooth).

### Registered cube fixture

Generate or non-destructively check the normal-save Blender 5.2.2 cube:

```powershell
$blender = Join-Path $env:ProgramFiles 'Blender Foundation\Blender 5.2\blender.exe'
& $blender --background --factory-startup --disable-autoexec --python-exit-code 1 --python .\tests\fixtures\generate_cube.py
& $blender --background --factory-startup --disable-autoexec --python-exit-code 1 --python .\tests\fixtures\generate_cube.py -- --check
```

The generator requires one active Scene, one visible identity-transform Cube,
six quads, eight two-meter corner points and one render UV map. `--check`
regenerates only in temporary storage, then reopens the committed input and
compares its points, topology, corner normals and UVs without rewriting it.
Full-file saved addresses/UI state are not byte-reproducible. The
[fixture record](../../plugins/usdBlendFileFormat/tests/fixtures/README.md#normal-save-cube)
owns provenance. The bundle's smoke/roundtrip/golden pyramid uses this input.

#### Direct usdview rendering

Use an installed imaging runtime with `usdview`, Storm, a compatible host
Python, Qt and PyOpenGL. The local `lookdev` profile supplies these; it is
separate from the `usd` profile used by the regular regression commands.
Build the bundle against the runtime being loaded.

The following commands were exercised on Windows on 2026-10-05 with OpenUSD
26.08, Python 3.13, PySide6 6.8.3 and PyOpenGL 3.1.9:

```powershell
ost plugin build plugins\usdBlendFileFormat --target cy2026 --profile lookdev
$usdBin = Join-Path $HOME '.ost\runtimes\openstrata-cy2026-windows-x86_64-py313-lookdev\bin'
ost plugin run plugins\usdBlendFileFormat --target cy2026 --profile lookdev -- python "$usdBin\usdview" plugins\usdBlendFileFormat\tests\fixtures\single_cube.blend --defaultsettings --select /Asset/geo/Cube/mesh --renderer Storm --quitAfterStartup
ost plugin run plugins\usdBlendFileFormat --target cy2026 --profile lookdev -- python "$usdBin\testusdview" plugins\usdBlendFileFormat\tests\fixtures\single_cube.blend --renderer Storm --select /Asset/geo/Cube/mesh --testScript plugins\usdBlendFileFormat\tests\test_usdview.py
```

The first launch checks direct `.blend` startup and exits automatically.
The second uses OpenUSD's existing `testusdview` harness to verify the loaded
file-format identity, `/Asset/geo/Cube/mesh`, Y-up/meter metadata, Storm
convergence within 30 seconds, a visible Cube against a black background and
a center-pixel pick of that Mesh. It uses a wider three-quarter test camera
to expose the silhouette, disables HUD/bounding-box overlays, and closes the
viewer after checking. It fails with `--norender`; opening a stage alone is
not rendering evidence.

The Python launcher is explicit because `ost` 0.23.14 cannot directly execute
the extensionless `usdview`/`testusdview` scripts on Windows (Win32 error 193).
To inspect interactively, omit `--quitAfterStartup` from the first command
and orbit/zoom as needed. Neither launch rewrites or exports the `.blend`.
Set `USD_BLEND_USDVIEW_SCREENSHOT` to a writable PNG file path before the
harness command to retain its viewport capture; an unwritable path fails
the check. The default invocation does not write a screenshot.

These GPU/Qt checks are local and optional, not part of the hosted
stage-contract workflow. The exact Cube milestone run and hosted source SHA
are recorded with the [fixture evidence](../../plugins/usdBlendFileFormat/tests/fixtures/README.md#cube-milestone-verification).

### Multi-scale unit and naming oracles

Run with each pinned Blender installation (4.5.13 or 5.2.2). Default outputs
are the four `unit-*.blend` files and adjacent UTF-8 oracles in
`tests/fixtures/native-units/blender-<version>/`. Cases, provenance, naming
expectations and comparison thresholds are in the
[unit fixture record](../../tests/fixtures/native-units/README.md).

```powershell
$blender = Join-Path $env:ProgramFiles 'Blender Foundation\Blender 4.5\blender.exe'
& $blender --background --factory-startup --disable-autoexec --python-exit-code 1 --python .\tests\fixtures\generate_units.py
& $blender --background --factory-startup --disable-autoexec --python-exit-code 1 --python .\tests\fixtures\generate_units.py -- --check
& $blender --background --factory-startup --disable-autoexec --python-exit-code 1 --python .\tests\fixtures\test_generate_units.py

$blender = Join-Path $env:ProgramFiles 'Blender Foundation\Blender 5.2\blender.exe'
& $blender --background --factory-startup --disable-autoexec --python-exit-code 1 --python .\tests\fixtures\generate_units.py
& $blender --background --factory-startup --disable-autoexec --python-exit-code 1 --python .\tests\fixtures\generate_units.py -- --check
& $blender --background --factory-startup --disable-autoexec --python-exit-code 1 --python .\tests\fixtures\test_generate_units.py
```

`--check` regenerates only in temporary storage, then reopens each saved input
without rewriting fixtures or oracles. Regression tests cover different-process
and different-path reproduction, home-path exclusion, changed-oracle rejection
and saved scale, transform, geometry, name and UV mutations. The full-file bytes
are not reproducible. `usdBlend.units` runs in both root and standalone bundle
CTest modes above, without Blender at test time.

### Saved normal oracles

Run with each pinned Blender installation (4.5.13 or 5.2.2). The default
output directory holds `smooth.blend`, `flat.blend`, `split.blend`,
`custom.blend`, `custom_fans.blend`, `custom_split_fans.blend`,
`custom_angles.blend`, `multi.blend`, `constant.blend` and adjacent `.oracle.txt` files.
Blender 5.2 additionally generates `polygon_smooth.blend` and
`polygon_split.blend`; the older-version defaults are unchanged.
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

To generate or check only the Blender 5.2 polygon-fan evidence, then run its
native comparison without requiring Blender at test time:

```powershell
$blender = Join-Path $env:ProgramFiles 'Blender Foundation\Blender 5.2\blender.exe'
& $blender --background --factory-startup --disable-autoexec --python-exit-code 1 --python .\tests\fixtures\generate_normals.py -- --groups polygon_smooth polygon_split
& $blender --background --factory-startup --disable-autoexec --python-exit-code 1 --python .\tests\fixtures\generate_normals.py -- --check --groups polygon_smooth polygon_split
ctest --preset reader -R '^blendScene\.polygonNormals$' --output-on-failure
```

`polygon_smooth` and `polygon_split` share concave/nonplanar polygon pairs,
unequal-area triangles, disconnected vertex fans and 35 angle-sweep wedges.
An additional flat triangle selects connected split fans in the second file.
The normal-oracle executable compares 495 corners at the unchanged `2e-5`
threshold and pins exact reversed-block equality in both build modes.
Registered-plugin tests compare those saved normals and preserve metadata,
repeat and referenced reads. The generator regression suite rejects saved
polygon-coordinate and flat-face-selector mutations without rewriting files.

The generator constructs source geometry without modifiers. All `custom*`
groups record Blender's resulting corner normals and packed short pairs.
The fan fixtures exercise shared spaces and integer averaging; the angle
fixture pins the observed reference-angle approximation independently of
native decoder math. `multi` creates separate Mesh datablocks, not copies or shared
Object data, with uniform scaling and translation of the second Mesh's points.
`constant` creates two independent flat wedges and calls `shade_flat` after
removing dense sharp-face attributes, producing constant `AttributeSingle`
booleans in 5.2.2 and legacy CustomData in 4.5.13.
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

To check only the constant-storage controls without rewriting them:

```powershell
$blender = Join-Path $env:ProgramFiles 'Blender Foundation\Blender 4.5\blender.exe'
& $blender --background --factory-startup --disable-autoexec --python-exit-code 1 --python .\tests\fixtures\generate_normals.py -- --check --groups constant
$blender = Join-Path $env:ProgramFiles 'Blender Foundation\Blender 5.2\blender.exe'
& $blender --background --factory-startup --disable-autoexec --python-exit-code 1 --python .\tests\fixtures\generate_normals.py -- --check --groups constant
```
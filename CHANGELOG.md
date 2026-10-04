# Changelog

All notable changes to this project are recorded here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and versions follow
[Semantic Versioning](https://semver.org/).

## [Unreleased]

### Fixed

- Modern source normal corner weights now share the independently measured
    Blender angle approximation with custom normal reference spaces instead
    of mathematical `acos`. A concave shared corner previously exceeded the
    existing `2e-5` normal-component threshold. Two Blender-written 5.2.2
    polygon-fan fixtures cover 495 corners, nonplanar polygons, unequal-area
    triangles, disconnected fans and 35 angle-sweep wedges in point/split
    domains; maximum error is below `1.6e-6`. Native and registered-plugin
    oracles, metadata/repeat/reference reads and both build modes cover the
    correction. Existing custom-normal error improves below `3e-7`; Blender
    3.3 retains its separately tested mathematical weights.

### Added

- Fixture-backed Blender 3.3 auto-smooth normals using four independently
    generated 3.3.21 saved files at 0/60/90/180 degrees. Observed Mesh flag
    `0xd120` and scalar-float `smoothresh` select angle-limited connected
    fans through the existing sharp/flat split-normal path. Native and
    registered-plugin oracles compare 644 corners within `2e-5`; measured
    maximum component error is below `8.24e-8`. Four-layout regressions pin
    invalid-angle fatal context and inactive-angle behavior; generator checks
    reject saved mode/angle/sharp/flat mutations without rewriting fixtures.
    Both build modes cover USD authoring, metadata, repeats and references.
    Default legacy and modern normals are unchanged; older packed custom
    normals and other 3.x–4.4 versions remain outside verified scope.
- Accepted caller-overridable full-stream compression policy, resolving
    BLEND-O5 from locally generated Blender 4.5.13/5.2.2 large-mesh,
    repetitive-attribute and packed-image measurements. Standard budgets are
    256 MiB input, 512 MiB output, expansion ratio 4,096 and an 8 MiB Zstandard
    window. A reproducible generator, reader measurement mode, regression
    checks and dated evidence accompany the decision. Explicit API/CLI limits,
    header probes and uncompressed-only scene importing remain unchanged.
- Fixture-backed Blender 3.3 legacy Mesh decoding using the unchanged 3.3.21
    Mesh-domain file and saved Scene oracle. Signed MLoop indices, absent
    modern members and validated CustomData/fixed-array aliases compose into
    owning points, topology, transforms, shared Meshes and exact indexed
    MLoopUV maps. The older default normal mode includes flat-face contributions
    in smooth point normals without sharp-edge splitting; unsupported Mesh
    flags and packed custom normals fail explicitly. Native and registered
    plugin oracle checks cover empty warnings, extent, metadata, references
    and repeated/reversed determinism in both build modes. Modern member/core
    validation remains strict; other 3.x–4.4 versions and broader legacy
    normal modes remain unclaimed.
- Blender-written 3.3.21 legacy Mesh storage-only fixture and saved oracle.
    `blendScene.legacyMeshStorage` compares independent raw MVert/MPoly/MLoop
    and MLoopUV values, normal flags, empty pointers, UV zero signs/render
    selection, membership and sharing across repeated/reversed reads in both
    build modes. Observed MLoop indices are signed `int`, unlike the synthetic
    decoder's original `uint` shape. This initial storage-only milestone pinned
    contextual fatal unsupported-version rejection without partial IR, before
    the fixture-backed decoding above. Mesh generation/check regressions
    admit this pinned Blender build without broadening other generators.
- Optional direct `.blend` viewport regression using OpenUSD's `testusdview`
    harness: the registered Cube must render with Storm, converge within a
    bounded wait, remain visible against a black background and resolve a
    center pick to `/Asset/geo/Cube/mesh`. Stage-only `--norender` launches
    fail explicitly; optional PNG capture failures are reported. Windows
    GPU verification and matching Windows/Linux hosted Mesh/Empty regression
    evidence complete the Cube milestone without broadening decoder scope
    or introducing compressed-input defaults.
- Native legacy fixed-array Mesh geometry through SDNA `MVert`, `MLoop`,
    `MPoly` and normal-related `MEdge` flags. Explicit legacy storage selection
    preserves modern attribute/offset authority without rescuing malformed or
    partial modern cores. Four synthetic pointer-width/byte-order layouts
    cover owning/shared positions and topology, flat/smooth/mixed and sharp
    normals, packed automatic normals, indexed float2/MLoopUV maps, loose
    points, unit independence, determinism and contextual fatal diagnostics.
    This initial boundary had synthetic evidence only; the 3.3.21 decoding
    above adds the separate Blender-written evidence.
- Blender-written 4.5.13/5.2.2 Mesh-domain oracles and `blendScene.meshFixture`,
    covering empty Meshes, loose points, UV-free polygons, corner seams,
    out-of-range and signed-zero UVs, constant coordinates and distinct
    editing/render maps. Owning native reads pin exact UV indexing, empty
    warnings and repeated/reversed-block determinism; registered-plugin
    tests cover geometry, extent, metadata-only reads and references.
    Blender 5.2 zero-domain dense AttributeArrays now accept unused nonzero
    data keys without serialized DATA blocks, while nonempty and constant
    payload/reference validation remains strict. Four-layout regressions
    retain exact fatal size/flag/reference context. Generator checks reject
    saved UV sign, render-map, loose-point and sharing mutations without
    rewriting fixtures; Scene-oracle UV comparisons now preserve signed zeros.
- Uncompressed registered-plugin native decoding and Scene IR authoring,
    preserving Mesh/Empty hierarchy, local transforms, render visibility,
    polygon geometry, normals and indexed UVs without a second normalization.
    Structural byte/block/graph bounds derive from stored size and block count;
    compressed inputs remain rejected without BLEND-O5 production defaults.
    Fatal and recoverable diagnostics retain byte/block/datablock context.
    Metadata-only layers retain transforms and typed Mesh children but omit
    geometry attributes; lazy decoding is deferred. Registered Scene-oracle,
    multi-scale, deterministic/reference and input-diagnostic regressions join
    both build modes' byte-to-Scene/authoring checks. A reproducible semantic
    `single_cube.blend` and Mesh golden replace header-only and Scene-library
    smoke successes, which now fail explicitly without scene fallbacks.
- Eight Blender-written 4.5.13/5.2.2 multi-scale cube/parenting fixtures and
    `usdBlend.units`, proving native-to-USD physical equivalence for source
    scales 1, 0.01, 0.001 and 10. Independent saved-value and world-vertex
    oracles pin one-meter points, extent and dimensions, normalized world/local
    matrices, unchanged normals/UVs and fixed USD units. Fixture-backed ASCII
    Object/UV collisions, Japanese fallbacks, exact UTF-8 provenance/display,
    child reservations and repeated/reversed-read determinism freeze STAGE-O1
    and NAME-O1. Both build modes include the CTest; companion CI runs it.
    Native decoding now accepts cached negative-world-handedness bit 2 without
    applying reflection twice; every other non-instance flag remains rejected.
    Generator regressions verify reproduction and non-destructive saved-value
    checks. The importer remains header-only; no production budgets are added.
- Blender-written 4.5.13/5.2.2 integrated native Scene fixtures and
    `blendScene.sceneFixture`, combining selected Mesh/Empty membership,
    four-level parenting, shared Mesh indices, an unselected Mesh parent,
    own render bits and fixed-child naming with world/local transforms,
    points, polygon topology, mixed normals and two indexed UV maps.
    Saved Blender oracles compare owning IR after reader inputs are released;
    repeated and reversed-block loads retain every IR value. Generator checks
    cover semantic reproduction, non-destructive checks and saved transform,
    Mesh-sharing and UV mutations. Root reader/Scene and standalone Scene
    suites retain their dependency gates. Production decoding and the
    header-only importer are unchanged; this fixed-scale native evidence
    does not resolve STAGE-O1 or introduce production budgets.
- Internal Scene IR-to-USD authoring in `usdBlendFileFormat`, with Object
    Xforms, preserved parenting/render visibility, transposed parent-relative
    matrix ops, duplicated polygon Meshes, extent, face-varying normals and
    indexed texCoord2f UVs. Shared Object/UV naming logic reserves render `st`,
    retains source provenance/display names and orders output by unsigned
    source bytes. Invalid IR and float-range values fail without a partial
    layer; singular parents remain explicit errors. Synthetic regressions and
    unchanged Blender-written transform/two-Mesh fixtures cover native-to-USD
    values, oracle matrices, references and repeat/reordered determinism.
    The allowed Scene library edge is wired through both build modes and the
    bundle manifest, with root and standalone CTests and companion CI coverage.
    The shared metadata scaffold preserves existing header-stage goldens.
    The importer remains header-only; production input budgets, STAGE-O1,
    NAME-O1 and end-to-end cube integration remain open.
- Native legacy type-16 `MLoopUV` UV decoding in `blendScene`, using complete
    SDNA record validation and embedded float-pair coordinates rather than
    a packed float2 or host-structure assumption. The existing owning indexed
    UV and render-map semantics apply without axis flips or unit scaling;
    record selection/pinning flags are ignored. Four synthetic layouts cover
    two maps, mixed float2/MLoopUV layers, member offsets, shared geometry,
    ownership, repeated/reordered reads, empty domains and exact fatal
    malformed-storage/value diagnostics. No Blender-written MLoopUV fixture,
    legacy fixed position/topology fallback or USD importer change is claimed.
- Deterministic Mesh/Empty Object identifiers in `blendScene`, connected to
    native `DecodeScene` before publication. ASCII sanitization preserves
    literal underscores, compares raw source bytes independently of locale
    and enumeration, resolves sibling and natural-suffix collisions, and
    reserves `mesh` only under Mesh parents. Raw source names, graph/discovery
    order, shared geometry and normalized transforms remain unchanged.
    Separate UTF-8 display repair reports malformed names without changing
    source bytes; duplicate sibling names and invalid immediate IR references
    fail explicitly. All 40,320 permutations of an eight-Object Scene,
    contextual four-layout native regressions and unchanged Blender-written
    transform fixtures pass, as do all nine Scene CTests in root and standalone
    builds. NAME-O1 remains open; USD name authoring and the header-only
    importer are unchanged.
- Native constant Mesh attributes in `blendScene`: logical single-value
    AttributeArrays and Blender 5.x AttributeSingles expand positions,
    integers, face/edge booleans, indexed UVs and packed custom normals through
    the existing typed readers, without another basis or unit conversion.
    Dense validation remains strict; invalid flags, logical sizes, one-value
    payload lengths/counts and references fail with exact source context.
    Both forms have raw/structured regressions across four synthetic layouts.
    Two new Blender-written 4.5.13/5.2.2 two-Mesh fixtures pin flat-normal
    oracles and the modern one-byte AttributeSingle shape. In-memory Single
    collisions exercise the existing Mesh ownership boundary, with matching
    storage discriminators; the global reader map remains strict.
    Single-flag AttributeArrays have synthetic evidence only. The importer
    remains header-only; no USD geometry or legacy UV storage is introduced.
- Native packed custom split normals and explicit 5.x Mesh-owned Attribute
    address resolution in `blendScene`. Different collided payloads resolve
    only through serialized ownership and validated Mesh/Attribute references;
    the reader's global map and all other duplicate rejection remain strict.
    Custom normals reconstruct signed-short pairs in source fan spaces,
    including automatic values, signed minima and integer fan averaging.
    Independent RNA measurements and six additional Blender-written fixtures
    pin reference-angle math, shared/open/closed fans and 199 triangle angle
    cases per version. All 1,588 custom corners compare at the existing `2e-5`
    component threshold (maximum measured error below `6.25e-6`), and both
    independent multi-Mesh fixtures now match their saved oracles. Root and
    standalone Scene suites, dependency gates and both Blender generator
    regressions pass. Constant storage, other named normal formats and USD
    geometry authoring remain separate work; the importer stays header-only.
- Blender-written 4.5.13/5.2.2 packed custom-normal and independent two-Mesh
    boundary fixtures, with saved corner-normal and signed-short RNA oracles.
    `blendScene.meshBoundaries` pins exact packed storage and explicit
    no-partial-Scene rejection, and proves 5.2.2 Attribute/AttributeArray
    address collisions have different payloads even when their shapes match.
    Mapping, selection and decoding retain fatal context under reversed
    enumeration. The 4.5.13 two-Mesh control compares native arrays at the
    existing normal threshold. Generator checks cover cross-process/path
    reproduction, saved-content changes and selected-group isolation without
    rewriting existing fixtures. No production pointer-map or decoder behavior
    is changed.
- Native smooth point normals and sharp-edge/flat-face split corner fans in
    `blendScene`, using corner-angle weights and one basis rotation without
    unit scaling. Split topology, malformed storage and degenerate/cancelling
    normals fail explicitly. Six unchanged Blender-written 4.5.13/5.2.2
    single-Mesh fixtures cover 15 geometry cases and 137 corners per version;
    saved oracles and repeated/reordered reads compare in `blendScene.normals`.
    Both storage forms across four synthetic layouts cover angle weights,
    missing sharp-face defaults, unit independence and contextual failures.
    Fixture generator/check regressions and both root/standalone seven-test
    Scene suites pass. Custom normals and repeated 5.x Attribute saved addresses
    remain outside this boundary; pointer validation and the header-only
    importer are unchanged.
- USD-independent `ParentRelativeTransform` in `blendScene`, constructing
    affine locals from already-normalized world matrices without a second
    basis/unit conversion or an explicit inverse. Row scaling and pivoting
    retain shear and nonuniform/negative scales; singular/numerically singular
    parents, invalid inputs and overflow fail explicitly without hierarchy or
    identity fallbacks. Synthetic regressions and unchanged Blender-written
    4.5.13/5.2.2 transform oracles cover local matrices, world reconstruction,
    singular roots/children and repeated/reordered reads. Both root and
    standalone library builds and their six Scene CTests pass. Scene IR remains
    world-only; USD authoring and the header-only importer are unchanged.
- Native Mesh/Empty Scene IR decoding in `blendScene`, with source points,
    polygon topology, flat face-varying corner normals and named indexed UVs.
    The real 4.5.13 CustomData and 5.2.2 AttributeArray Cube Mesh payloads and
    both storage forms across four synthetic layouts cover shared Mesh indices,
    ownership, render UV selection, repeated/reordered reads, meter normalization
    and contextual empty/invalid/unsupported storage diagnostics. Mesh modifiers
    and shape keys report unapplied evaluation without changing source geometry.
    Smooth/custom split normals and constant/legacy storage remain explicitly
    unsupported. No committed fixture bytes, identifiers, backend or USD/header-only
    importer behavior are changed; both root and standalone library checks pass.
- Library-only native `DecodeScene` in `blendScene`, composing saved Object-value
    and recursive graph validation into owning Empty Scene IR with source names,
    render visibility and selected parent indices. XYZ Euler and delta channels,
    ordinary parent inverses and parent-only transforms construct source world
    matrices before one meter/basis conversion. Unsupported kinds, Image Empty
    data, enabled instances and other transform modes fail explicitly without
    partial IR; animation/constraint presence reports unapplied evaluation.
    Four synthetic layouts cover ownership, scales, reordered/shared/deep parents,
    exact budgets, malformed/nonfinite storage and source/unit overflow; both
    corpus SDNA layouts are exercised with in-memory Empty-kind/data mutations.
    No fixture bytes, identifiers, mesh decoding, backend or header-only importer
    behavior are changed.
- Opt-in saved Object value validation in `blendScene`, retaining source type,
    render visibility, transform flags and immediate instance Collection block
    indices while preserving generic Object selection. Type-specific data
    code/SDNA requirements and instance references fail with bounded contextual
    diagnostics. Four synthetic layouts cover mappings, parent-only values,
    shared targets and exact budgets; both Blender-written corpus files cover
    saved visibility storage differences and data/instance/flag mutations.
    No transform construction, recursive instance expansion, populated Scene IR,
    USD authoring or fixture bytes are changed.
- Bounded saved Object data ID-reference validation in `blendScene` selection,
    accepting stored `void *` and `ID *` declarations and returning optional
    caller-sequence data block indices. Selected and parent-only Objects share
    exact-address, local-ID and explicit visit-budget checks without partial
    output. Four synthetic layouts cover shared/reordered data, malformed
    references and budget boundaries; both corpus files resolve Camera/Cube/Light
    to Camera/Mesh/Lamp and cover pointer mutations. This adds no Object-kind
    data requirements, data-internal traversal, Mesh values, instance graph,
    populated Scene IR or changes to fixture bytes or the header-only importer.
- Bounded saved Object parent-reference validation in `blendScene` selection,
    returning optional caller-sequence parent block indices without adding
    parent-only Objects to Collection membership. Exact SDNA references and
    explicit visit/depth budgets reject invalid/linked parents and cycles with
    fatal source context. Four synthetic layouts cover shared/unselected parents
    and deep chains; both corpus files cover null parents and pointer mutations.
    No data/instance references, parenting modes, transforms, populated Scene IR,
    fixture bytes or header-only importer behavior are changed.
- Bounded saved Collection membership selection in `blendScene`, returning
    owning Object names and block indices from the active Scene's master
    Collection. Explicit visit/depth limits, exact references and ListBase
    consistency reject linked IDs, invalid targets and cycles without partial
    results. Both corpus files and four synthetic layouts cover membership,
    shared records, ownership, deep chains and fatal diagnostic context.
    No Object values, parent/data/instance references, transforms, populated
    Scene IR, fixture bytes or header-only importer behavior are changed.
- Native saved-scene selection in `blendScene`, using `FileGlobal.curscene`,
    exact saved-address resolution and SDNA, with owning source name/version/unit
    metadata and contextual fatal diagnostics for missing, invalid or linked
    active scenes. Real 4.5.13/5.2.2 corpus and four synthetic layouts prove
    selection without a first-Scene or library fallback. Standalone and composed
    packages declare the reader dependency and preserve forbidden-edge gates.
    No Collection walk, object/mesh decoding, populated Scene IR or importer
    geometry integration is added.
- Borrowed SDNA block/member/array views, saved-pointer reads and explicitly
    typed integer/IEEE floating-point reads, with checked ranges, element
    counts and contextual fatal diagnostics. Regressions cover both pointer
    widths and byte orders, numeric boundaries, malformed access and real
    Scene unit fields. Both normal-save corpus files' current Scene pointers
    resolve through the existing map; the Scene-only library fixture's null
    pointer is preserved. No Scene-selection fallback, graph traversal, Scene
    IR publication or changes to the header-only importer are added.
- Validated Scene IR unit conversion for stored distances, positions and
    affine mesh/empty world translations, with meter normalization before
    basis rotation and no dimensionless scale changes. Synthetic regressions
    cover four source scales, equivalent cubes, parent-child composition and
    invalid/overflow failures. The selected stage policy keeps
    `metersPerUnit = 1`; Blender-written equivalence and native scene/USD
    integration remain required before STAGE-O1 is frozen.
- The OpenUSD-independent `blendScene` static library, with an owning
    object/mesh Scene IR, parent and shared-mesh indices, source metadata and
    point/world-matrix basis conversion. Synthetic tests cover ownership,
    asymmetric transforms, parent-child composition and right-handed winding;
    standalone/composed builds install its CMake package and enforce include
    and generated-link boundaries through shared reader test helpers. This
    adds no native scene decoder or USD geometry to the header-only importer.
- Reproducible compression measurements for the generated empty scene and
    both real corpus files, with pinned stored/decoded sizes and minimum
    integer-ratio/window allowances. Reader regressions require identical
    output at exact budgets and matching diagnostics immediately below them;
    the small corpus does not establish production decompression defaults.
- A verified inspection guide for `blend_inspect`, covering summaries, block
    and SDNA listings, raw Object names, explicit compressed-input budgets,
    diagnostic context and normal/error exit codes on committed fixtures.
- Malformed-input reader regressions across every block in the generated
    empty scene and both real corpus files, plus every synthetic container
    layout: boundary/header-prefix and payload cut points, oversized lengths,
    checked source-read ranges and exact fatal diagnostic context. Every
    real-file ID is also checked with out-of-range SDNA indices, without
    changing reader behavior or the header-only importer.
- The OpenUSD-independent `blend_inspect` workspace tool, with fixture-backed
    header/layout summaries, raw ID counts, block and SDNA listings and saved
    Object names. Its standalone/composed CMake project stages `bin/` for
    workspace packaging and installs the executable; compressed inputs require
    explicit byte, expansion-ratio and window limits. CLI regressions cover
    argument errors, diagnostics, malformed DNA1 and Windows UTF-8 paths.
- An owning exact-key pointer map for ID and DATA blocks, with null and
    unresolved-reference handling, duplicate-key diagnostics and exclusion of
    metadata address collisions. Raw datablock enumeration uses SDNA to list
    every ID's type and stored name in the generated empty scene and both real
    corpus files, preserving prefixes and byte values without graph traversal
    or changes to the header-only importer. Synthetic regressions cover all
    pointer-width/byte-order layouts and malformed ID ranges and metadata.
- Owning SDNA schema decoding with NAME/TYPE/TLEN/STRC tables, member offsets,
    multidimensional arrays, pointer and function-pointer storage, and lookup
    by struct/member name. Regressions decode every structure in the generated
    empty scene and both real corpus files, cover all synthetic pointer-width
    and byte-order combinations, and reject malformed payloads with
    `BLEND_DNA_*` diagnostics without changing the header-only importer.
- A contributor-provided Blender 4.5.13 legacy corpus file, preserving the
    original bytes with explicit commit permission and manifest provenance.
    Reader regressions cover its header, full-file byte round trips, known
    block positions and identical block-kind sets with the 5.2.2 corpus,
    without claiming SDNA or scene compatibility.
- Explicit-limit block enumeration for legacy and Blender 5 containers,
    normalized to one record with validated framing, terminal `ENDB`,
    recoverable unknown-code diagnostics and no payload allocation. Tests
    cover all legacy widths and byte orders, sparse 64-bit fields, malformed
    boundaries, decoded compression round trips and real Blender 5 fixtures.
- Explicit-limit full-file byte reading for uncompressed, gzip and Zstandard
    sources, with owning output, validated headers, complete member/frame and
    checksum checks, bounded output growth, and input/output/ratio/window
    diagnostics. Regression tests cover real-file byte round trips, failed
    reads, exact limits, truncation, trailing garbage and high-ratio streams.
- A bounded gzip header probe using the vendored zlib decoder, with legacy
    and format-1 layouts, concatenated members, input limits, and regression
    tests for truncation, corruption, metadata, source failures and stopping
    before payload or trailer validation.
- Vendored zlib 1.3.2 inflate and checksum sources, with archive provenance,
    private prefixed symbols, installed license notices and a gzip decoder
    smoke test.
- Header regression tests for the 2.99/3.0 version boundary across legacy
    pointer widths and byte orders, and for Blender 4.5 format-1 headers with
    and without Zstandard compression.
- Root Apache-2.0 license, contribution guidelines, security reporting policy
    and Code of Conduct, linked from the repository and documentation indexes.
- The `usdBlendFileFormat` bundle from OpenStrata's `usd-fileformat-cpp`
    template, pinned to OpenUSD 26.08, with ArResolver-backed header validation
    and the minimal `/Asset` stage, contract version 1 and kind `component`.
- The OpenUSD-independent `blendFile` static library: bounded file and memory
    byte sources, diagnostic records, `Result<T>` and the legacy header reader.
- Root dual-mode CMake build, reader-only presets, workspace/library manifests,
    byte-level fixture generator, stage contract tests and an L5 golden.
- A reader boundary gate over generated CMake File API link fragments, with
    CTest metadata setup and allowed/forbidden dependency regression checks.
- Blender 5 format-1 header validation and a bounded Zstandard header probe,
    with container-version/header-size metadata and regression tests for the
    real corpus file, truncation, corrupt streams, frame splits and limits.
- The missing upstream Zstandard 1.5.7 error header required by its public API.
- A Blender 5.2.2 LTS empty-scene fixture and generator, with non-destructive
    Blender content validation, real-file stage contract tests, and L3-L5
    verification against a second golden.
- A Windows/Linux OpenStrata CI matrix and generated pull-request workflow,
    with digest-pinned OpenUSD 26.08 SDKs, workspace graph and reader CTest
    gates, and standalone bundle L0-L5 checks.
- A companion Windows/Linux workflow for explicit plugin doctor and all five
    stage-contract checks, resolving the existing CI bundle cells instead of
    copying their SDK pins and preserving diagnostic reports and test logs.
- A build guide for reader-only, standalone bundle and plain CMake workflows,
    including fixture generation and validation.
- The documentation baseline: the design policy, the stage, blend, material,
  naming and backend contracts, the workspace contract, external dependencies,
  the capability matrix and diagnostics reference, the roadmap, and the
  documentation guidelines.

### Changed

- Narrowed the current support guarantee, first stable release target and new
    compatibility fixtures to Blender 5.x. Full older-Blender compatibility
    moves to Phase 8, where its version and feature scope will be selected.
    Existing older-version decoders, fixtures and regression tests remain;
    runtime version handling and import behavior are unchanged.
- Corrected a missing byte in the independent DEFLATE test vector; full-stream
    tests now verify its CRC and stored size as well as its header.
- The dependency contract resolves zlib through a vendored decoder subset,
    without relying on the OpenStrata OpenUSD runtime or an externally
    installed zlib package, including for installed reader targets.
- The blend contract distinguishes the native decoder's 3.0 design floor
    from structural header recognition, and records the fixture requirements
    for legacy big-endian and 32-bit-pointer compatibility claims.
- The blend contract records the Blender 5 format-1 block-header offsets,
    signed 64-bit lengths and counts, and contiguous block boundaries,
    confirmed against the tagged format definition and the generated fixture.
- Documentation status is centralized: phase status and release mapping in the
    roadmap table, incomplete task status in the current roadmap, and supported
    behavior in the capability matrix. Other pages link to these owners rather
    than maintaining parallel progress summaries.
- The Blender empty-scene generator writes only the Scene and its dependencies
    through the library writer, avoiding UI serialization. The regenerated
    fixture is byte-identical across independent processes on the verified
    Windows build, with a non-destructive `--check-bytes` mode and three
    regression tests covering reproduction, modification detection and
    inspection of the stored Scene.
- Plain CMake accepts the caller's installed OpenUSD SDK without a version
    constraint. The OpenStrata verification runtime remains pinned to 26.08;
    accepting another SDK is not a compatibility guarantee.
- Native Object rotation decoding for all six Euler orders, Quaternion and
    Axis-Angle, with active-channel shape/finite validation and Blender's
    mode-specific delta behavior. Repository-generated Blender 4.5.13/5.2.2
    transform fixtures compare 27 Empty objects per file against saved world
    and parent-local matrix oracles, including nonuniform/negative/zero scales,
    parent inverses, three-level hierarchy, render visibility and single-pass
    normalization. Root and standalone tests include the oracle comparison;
    generator regressions check semantic reproduction and non-destructive
    validation. USD authoring and the header-only importer are unchanged.

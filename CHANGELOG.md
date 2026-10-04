# Changelog

All notable changes to this project are recorded here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and versions follow
[Semantic Versioning](https://semver.org/).

## [Unreleased]

### Added

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

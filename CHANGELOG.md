# Changelog

All notable changes to this project are recorded here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and versions follow
[Semantic Versioning](https://semver.org/).

## [Unreleased]

### Added

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

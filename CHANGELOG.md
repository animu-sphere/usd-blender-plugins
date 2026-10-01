# Changelog

All notable changes to this project are recorded here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and versions follow
[Semantic Versioning](https://semver.org/).

## [Unreleased]

### Added

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

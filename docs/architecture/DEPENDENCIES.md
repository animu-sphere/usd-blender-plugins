# External dependencies

Every external dependency, why it exists, how it is found, and who may use it.
Dependency edges between components are
[WORKSPACE.md §2](WORKSPACE.md#2-dependency-directions).
Dependency decisions are recorded in [§7](#7-open-questions); generated-fixture
requirements belong to the
[fixture provenance](../../plugins/usdBlendFileFormat/tests/fixtures/README.md).

## 1. OpenUSD

| | |
| --- | --- |
| Used by | `usdBlendFileFormat` only |
| Found as | an installed package, `find_package(pxr CONFIG REQUIRED)`, resolved once at the root |
| Version | OpenStrata verification runtime: 26.08 (DEP-O1 resolved). Plain CMake: the caller's installed SDK, not version-pinned. |

The OpenStrata `usd-fileformat-cpp` template carries an OpenUSD version range
that is only its scaffold default. The bundle manifest pins the OpenStrata
verification runtime to 26.08. Plain CMake uses
`find_package(pxr CONFIG REQUIRED)` without a version constraint, allowing the
caller to supply another installed SDK. Accepting an SDK at configure time is
not a compatibility guarantee. Build
and load the plugin against the same SDK release, since OpenUSD has no stable
cross-release C++ ABI.

## 2. Compression

| Library | Reads | Used by | Found as |
| --- | --- | --- | --- |
| zlib 1.3.2 | gzip-compressed `.blend` | `blendFile` | vendored inflate and checksum sources in `third_party/zlib` |
| Zstandard 1.5.7 | Zstandard-compressed `.blend` | `blendFile` | vendored decompression-only source in `third_party/zstd` |

Both are permissively licensed and belong only to `blendFile`.
Both decoders are compiled directly into the static library, with no external
link dependency or build-time download. Their fixed releases, licenses, and
source provenance are recorded in
[third_party/zlib/README.md](../../third_party/zlib/README.md),
[third_party/zstd/README.md](../../third_party/zstd/README.md) and
[THIRD_PARTY_NOTICES.md](../../THIRD_PARTY_NOTICES.md).

zlib includes only inflate, its checksum routines, and required headers, not
the encoder or gzip file I/O. It uses upstream `Z_PREFIX` to avoid unprefixed
zlib symbol collisions with an OpenUSD SDK. Headers and compile definitions
remain private to the reader. Both plain CMake and OpenStrata builds use this
same source subset, including standalone reader builds without an OpenUSD
SDK. Installed reader targets need neither `find_dependency(ZLIB)` nor an
external zlib package. Licenses and provenance are installed with the library.
Reader support and fixture evidence, distinguishing bounded header probes
from full-container decompression, are recorded in the
[capability matrix](../reference/CAPABILITY_MATRIX.md#1-container).

## 3. Blender

Blender is **never** a build or runtime dependency of any component
([BACKEND_POLICY.md §7](../design/BACKEND_POLICY.md#7-license-boundary)). A
separately installed Blender executable is used for:

| Use | When | Version |
| --- | --- | --- |
| generating scene fixtures from committed scripts | by a maintainer, when fixtures change | the pinned version of each fixture directory |
| oracle tests | a dedicated CI lane and local runs; skipped elsewhere | the fixture's version |
| `blendHost` | Phase 7, optional at run time | configured by the user (BACK-O2) |

Generated fixtures are committed, so the normal test suite runs without
Blender.

## 4. Third-party code

The default is a purpose-built parser: `.blend` is a bounded binary format
whose structure is self-described by SDNA. A third-party `.blend` parser, or any
other third-party code, is adopted only after checking:

- license compatibility with Apache-2.0 — **no GPL code**, which excludes
  Blender's own source;
- maintenance status;
- malformed-input safety (bounds, overflow, fuzzing history);
- coverage of the Blender versions targeted
  ([BLEND_CONTRACT.md §9](../design/BLEND_CONTRACT.md#9-version-support));
- dependency weight;
- whether it exposes source facts without imposing Blender runtime semantics.

Vendored code goes in `third_party/`, created with its first content, and is
listed in `THIRD_PARTY_NOTICES.md`.

## 5. OpenStrata

`ost` scaffolds, builds, checks, tests and packages the bundle
([WORKSPACE.md §5](WORKSPACE.md#5-build-modes)). Nothing in CMake knows it: it
prepares the dependency prefix that a plain CMake caller would pass. The `ost`
version used is recorded in [the build guide](../guides/building.md).

## 6. Toolchain and platforms

| | |
| --- | --- |
| Language | C++20 (`std::span`, `std::filesystem`) |
| Windows | MSVC 2022, x64 |
| Linux | GCC or Clang, x86_64 |
| macOS | added once the reader is stable |

CI starts with Windows and Linux on the pinned OpenUSD release, and does not
grow the matrix (OpenUSD versions, compilers, Blender fixture versions) until a
reason is recorded.

## 7. Open questions

None currently.

### 7.1 Resolved decisions

- **DEP-O2, zlib portion (2026-10-02):** vendor the upstream 1.3.2 inflate
  and checksum source subset with provenance and its zlib license, as specified
  in §2. This replaces the separately installed `ZLIB::ZLIB` proposal. Do not
  assume that OpenStrata's OpenUSD artifact provides zlib or obtain it through
  OpenUSD's dependency graph. Keep the codec private to the reader and retain
  the independent installed-package contract. This decision does not claim
  gzip support.

- **DEP-O2, Zstandard portion (2026-10-01):** vendor the upstream 1.5.7
  decompression-only single-file library with provenance and its BSD license,
  following the `usd-vrm-plugins/third_party/cgltf` source-vendoring pattern.
  This decision does not claim full compressed-file support.

- **DEP-O1 (2026-10-01):** use OpenUSD 26.08 as the pinned OpenStrata
  verification runtime. The bundle manifest enforces that runtime choice;
  plain CMake deliberately does not restrict the caller's SDK version.

# External dependencies

> Status: **proposed**, 2026-10-01. Nothing is pinned yet; each section becomes
> binding when the Phase that first uses the dependency lands.

Every external dependency, why it exists, how it is found, and who may use it.
Dependency edges between components are
[WORKSPACE.md §2](WORKSPACE.md#2-dependency-directions).

## 1. OpenUSD

| | |
| --- | --- |
| Used by | `usdBlendFileFormat` only |
| Found as | an installed package, `find_package(pxr CONFIG REQUIRED)`, resolved once at the root |
| Version | one pinned release (DEP-O1) |

The OpenStrata `usd-fileformat-cpp` template carries an OpenUSD version range
that is only its scaffold default. The bundle manifest is updated to the
release this repository pins, and a mismatch is a configure error.

## 2. Compression

| Library | Reads | Used by | Found as |
| --- | --- | --- | --- |
| zlib | gzip-compressed `.blend` | `blendFile` | installed package |
| Zstandard | Zstandard-compressed `.blend` | `blendFile` | installed package |

Both are permissively licensed and are `blendFile`'s only external edges.
Whether they are taken from the OpenUSD build's own copies, from the system,
or from OpenStrata artifacts is DEP-O2.

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
version used is recorded with the first build guide.

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

| Id | Question | Proposed answer | Blocks |
| --- | --- | --- | --- |
| DEP-O1 | Which OpenUSD release is pinned? | 26.08, the release the sibling repositories pin, so the bundles compose in one runtime. | Phase 0 |
| DEP-O2 | Where zlib and Zstandard come from. | Installed packages on `CMAKE_PREFIX_PATH`, provided by OpenStrata artifacts in `ost` builds. | Phase 1 |

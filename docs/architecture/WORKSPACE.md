# Workspace contract

This document is the binding contract for how `usd-blender-plugins` is laid out
as an OpenStrata plugin workspace: component identities, their kinds and
directories, the dependency directions between them, manifests, build modes,
and the invariants every change preserves. **A structural change that
contradicts this document changes this document first, in its own pull
request** — never through a README, a roadmap entry, or code.

The identities below define architectural responsibilities, not implementation
status. Use the [capability matrix](../reference/CAPABILITY_MATRIX.md) for
implemented behavior and the [roadmap](../roadmap/README.md#status-at-a-glance)
for phase status. Scope is [DESIGN_POLICY.md §14](../design/DESIGN_POLICY.md#14-phases).

The shape follows the `usd-mmd-plugins` and `usd-vrm-plugins` workspace
contracts on purpose — the same plugin/library split, the same manifests, the
same build modes — so that a contributor, and an OpenStrata runtime, can treat
the repositories alike.

## 1. Identities

### 1.1 First target

| Identity | Kind | Directory | Manifest | Role | Created in |
| --- | --- | --- | --- | --- | --- |
| `blendFile` | plain static CMake library | `libs/blendFile/` | `openstrata.library.yaml` | Bounded `.blend` syntax: byte sources, diagnostics, compression, headers, blocks and SDNA. No OpenUSD. | Phase 0 (header), Phase 1 (container, SDNA) |
| `blendScene` | plain static CMake library | `libs/blendScene/` | `openstrata.library.yaml` | The Scene IR and the native backend: ID graph, version decoders, the single coordinate conversion, identifiers. No OpenUSD. | Phase 2 |
| `usdBlendFileFormat` | plugin bundle (`usd-fileformat`) | `plugins/usdBlendFileFormat/` | `openstrata.plugin.yaml` | Registration, resolver-backed byte access, validation and USD authoring through the file-format boundary. | Phase 0 |
| `blend_inspect` | CLI executable | `tools/blendInspect/` | `openstrata.tool.yaml` | Reports what a `.blend` contains — header, blocks, SDNA, datablocks, objects — without USD. | Phase 1 |

The reader's byte-level public boundary separates `ReadHeader` probes from
`ReadFileBytes`, which returns owning decoded bytes under explicit
`CompressionLimits`. Consumers can wrap those bytes in `MemoryByteSource`;
no codec types escape the library. The
[blend contract](../design/BLEND_CONTRACT.md#41-full-stream-byte-reading)
defines the limit and validation semantics.

`ReadBlocks` consumes an uncompressed `ByteSource` under an explicit block
count limit and returns normalized `BlendBlock` records without reading
payloads. No format-specific or codec types escape this boundary; its
[framing contract](../design/BLEND_CONTRACT.md#64-block-enumeration-boundary)
is separate from SDNA and scene validation.

`ReadDna` consumes a bounded DNA1 payload and its file header, returning an
owning schema with member layouts and name lookup. It does not resolve block
references or construct scene data; the
[schema contract](../design/BLEND_CONTRACT.md#71-schema-decoding-boundary)
defines this separate syntax boundary.

`BuildPointerMap` owns saved-address-to-block-index entries for ID and `DATA`
blocks, and `PointerMap::Resolve` diagnoses unresolved nonzero addresses.
`ListDatablocks` uses decoded bytes, block records and SDNA to return owning
raw ID type/name records. These remain syntax-only operations inside
`blendFile`; no graph traversal, scene construction or dependency edge is
added. Their [boundaries](../design/BLEND_CONTRACT.md#81-pointer-map-boundary)
separate raw references from the ID graph owned by `blendScene`.

`blendScene` exposes the owning object/mesh/material Scene IR and the single basis
conversion through `blendScene/Scene.h`, under the
[IR contract](../design/DESIGN_POLICY.md#521-scene-ir-foundation).
Its native saved-scene selection consumes reader syntax through the declared
`blendFile` edge; the IR header itself still uses only standard C++ types.
`blendScene/Selection.h` exposes the separate
[selection boundary](../design/DESIGN_POLICY.md#522-saved-scene-selection-boundary).
`blendScene/Decode.h` composes validated Object-value selection into owning
Scene IR through the
[native decoding boundary](../design/DESIGN_POLICY.md#527-native-scene-decoding-boundary).
Binary views and saved addresses stay inside the decoder; no OpenUSD or host
backend dependency is introduced.
The root adds the library before resolving OpenUSD. Its standalone CMake
package exports `blendScene::blendScene` with a public `blendFile` dependency
and resolves that installed package through `find_dependency`; OpenStrata
builds and installs the descriptor's reader prerequisite automatically.
Both build modes run the IR and forbidden-include tests, generated link
metadata and link-boundary checks, and rejection tests. The generated-link
test helpers are shared with `blendFile`, keeping the existing reader defaults.
The shared Scene-oracle executable also supplies `blendScene.meshFixture`
for Blender-written Mesh-domain and indexed-UV evidence, without adding a
library dependency or requiring Blender at test time.
The normal-oracle executable also supplies `blendScene.polygonNormals` for
Blender 5.2 concave/nonplanar polygon fans and source corner-angle weights.
Its saved fixtures run in both build modes without Blender or OpenUSD.
Constant Materials and per-Object effective slots are decoded into standard-C++
IR values under the [material boundary](../design/MATERIAL_POLICY.md#71-native-constant-material-boundary).
The `blendScene.materials` executable tests saved constants, shared-Mesh
overrides, face indices and malformed storage. Material naming reuses the
existing naming allocator. The plugin alone links `usdShade` to author
encapsulated `preview` NodeGraphs and bindings; `usdBlend.materials` tests that
boundary in root and standalone builds.
`Material.textures` adds owning standard-C++ external-image/UV/tangent-normal
values under the [texture boundary](../design/MATERIAL_POLICY.md#72-native-external-texture-boundary).
`blendScene.textures` reuses the material executable for saved image/node
storage and contextual failures. `usdBlend.textures` reuses the authoring
executable for standard texture/primvar-reader networks, UV naming, metadata and
invalid IR in both build modes. The native library still does not read pixels,
link imaging/USD libraries or use Blender at test time.
The Mesh-boundary executable also supplies `blendScene.legacyMeshStorage`:
unchanged Blender 3.3.21 raw storage/oracle checks and contextual failures for
signed-index, unverified-version and unsupported-normal-mode mutations.
The same file joins `blendScene.meshFixture` for owning legacy IR/oracle
comparisons; it adds no runtime dependency or new library edge.
The scene gate additionally allows `blendFile` and rejects OpenUSD, Blender,
host-backend and unknown libraries; the reader gate still rejects scene links.
The plugin now consumes the allowed `blendScene` edge for its internal
[Scene IR authoring boundary](../design/DESIGN_POLICY.md#531-scene-ir-usd-authoring-boundary),
declared in both CMake and its bundle manifest. An internal object target
shares authoring sources with the C++ regression executable; it is not an
installed library or a second consumer requiring `blendUsd`. Standalone
bundle builds resolve the installed Scene package and its reader prerequisite.
The same internal object target shares the bundle's `ReadScene`
composition with authoring and importer regression executables. The importer now consumes
`blendFile` syntax and `blendScene` native decoding before USD authoring;
the inspection tool still uses `blendFile` alone. No installed component or
dependency direction changes. The
[importer boundary](../design/DESIGN_POLICY.md#532-scene-importer-boundary)
preserves input-derived uncompressed byte budgets, explicitly selects the
accepted compression policy for gzip/Zstandard and derives block/graph
budgets from decoded bytes and enumerated blocks.

`blend_inspect` composes these syntax APIs through `blendFile` alone in
Phase 1. Its tool descriptor declares that library edge, and the root adds
`tools/blendInspect` before resolving OpenUSD. The standalone tool finds the
installed `blendFile` package; the composed build reuses the in-tree target.
Its executable is staged into the member's `bin/` and installed into the
prefix's binary directory. Workspace release membership includes the tool,
so aggregate packaging carries both the executable and file-format bundle.
The [CLI contract](../design/DESIGN_POLICY.md#54-blend_inspect--the-tool)
keeps raw Object listing separate from future scene decoding.

### 1.2 Later, only when their responsibility is real

Named now so the boundaries are designed for them; created only when the
condition in the last column is met. An empty architectural placeholder is not
created ahead of that.

| Identity | Kind | Directory | Role | Created when |
| --- | --- | --- | --- | --- |
| `blendHost` | plain static CMake library | `libs/blendHost/` | `IBlendBackend` over a separately installed Blender process ([BACKEND_POLICY.md §4](../design/BACKEND_POLICY.md#4-the-blender-host-backend)) | Phase 7 |
| `blendUsd` | plain static CMake library | `libs/blendUsd/` | USD authoring from a Scene IR, extracted from the bundle | a second consumer needs authoring without the file format ([DESIGN_POLICY.md §5.6](../design/DESIGN_POLICY.md#56-blendusd--deferred)) |

Naming follows the sibling repositories: libraries and bundles are lower-camel
identities equal to their directory name; executables are `snake_case` and
live in a lower-camel directory.

## 2. Dependency directions

### 2.1 Allowed edges

```text
blendFile ───────────→ compression libraries only      (no OpenUSD)
blendScene ──────────→ blendFile                        (no OpenUSD)
usdBlendFileFormat ──→ blendScene, blendFile, OpenUSD
blend_inspect ───────→ blendScene, blendFile           (no OpenUSD)

                       (later)
blendHost ───────────→ blendScene, blendFile            (no OpenUSD; Blender
                                                         only as a process)
usdBlendFileFormat ──→ blendHost                        (Phase 7)
blendUsd ────────────→ blendScene, OpenUSD
```

### 2.2 Forbidden edges

| Edge | Why |
| --- | --- |
| `blendFile → OpenUSD`, `blendScene → OpenUSD` | the reader is testable, fuzzable and reusable (CLI, converters, WASM) without OpenUSD |
| `blendFile → blendScene`, `blendFile → usdBlendFileFormat` | syntax never knows its consumers |
| `blendScene → usdBlendFileFormat` | the Scene IR never knows USD authoring |
| any component → a Blender library, Blender headers or Blender source | the license and ABI boundary is a process ([BACKEND_POLICY.md §7](../design/BACKEND_POLICY.md#7-license-boundary)) |
| any component → an HTTP client, an auth library, a cache service | asset transport is the resolver's ([DESIGN_POLICY.md §9](../design/DESIGN_POLICY.md#9-asset-resolution-boundary)) |
| a bundle → a sibling's source tree | siblings are consumed as installed packages (§5) |

### 2.3 Enforcement

A rule in prose is a convention; these are gates, added with the code they
guard:

- **Graph.** Every edge is declared in the component's manifest, so
  `ost plugin test --workspace --graph-only` rejects an undeclared or reversed
  edge before anything builds.
- **Link line.** Each plain library's tests check that its link line holds
  only its allowed edges — in particular, that `blendFile` and `blendScene`
  link no OpenUSD library.
- **Includes.** A boundary check scans each library's sources for forbidden
  includes (`pxr/`, Blender headers, a sibling's private headers).

All three run in CI from the Phase that creates the component.

Current gates: `ost plugin test --workspace --graph-only`, the reader's CMake
link-dependency check, `blendFile.boundary` include scanning, and
`blendFile.linkBoundary` inspection of generated CMake File API link fragments
for the reader test executable, including transitive libraries. The link gate
allows only `blendFile` and standard OS libraries; compression is currently
compiled into `blendFile`. `blendFile.linkMetadata` is its CTest setup fixture:
it reconfigures the existing build so even a fresh build has a File API reply.
`blendFile.linkBoundaryChecks` tests allowed and forbidden library fragments.
The [CI matrix](../../openstrata.ci.yaml) generates the
[source workflow](../../.github/workflows/ost-source-ci.yml): a graph job,
root CMake/CTest jobs on Windows and Linux, and standalone bundle L0-L5 jobs
on both platforms. The companion
[stage-contract workflow](../../.github/workflows/stage-contract-ci.yml) runs
explicit doctor and stage-contract checks. Procedures are in
[the build guide](../guides/building.md#ci-matrix).

## 3. Directory layout

Intended layout once Phases 0–2 have landed:

```text
usd-blender-plugins/
├─ .github/workflows/          generated from openstrata.ci.yaml; hand-written lanes
├─ cmake/                      shared CMake modules, one per concern
├─ docs/                       see docs/README.md
├─ libs/
│  ├─ blendFile/               include/ src/ tests/ fuzz/ CMakeLists.txt openstrata.library.yaml
│  └─ blendScene/              include/ src/ tests/ CMakeLists.txt openstrata.library.yaml
├─ plugins/
│  └─ usdBlendFileFormat/
│     ├─ plugin/resources/usdBlendFileFormat/   plugInfo.json
│     ├─ src/                  UsdBlendFileFormat.cpp, ArAssetByteSource.cpp, usd/
│     ├─ tests/fixtures/       the fixtures the plugin pyramid opens, and goldens
│     ├─ CMakeLists.txt
│     └─ openstrata.plugin.yaml
├─ tools/
│  └─ blendInspect/            src/ tests/ CMakeLists.txt openstrata.tool.yaml
├─ tests/
│  ├─ fixtures/                generators: byte-level scripts and Blender scripts
│  │  ├─ blender_45_lts/       written by Blender 4.5 LTS
│  │  └─ blender_5x/           written by Blender 5.x
│  ├─ integration/             stage-open tests across components
│  └─ installed_consumer/      a project consumed from outside the tree
├─ scripts/                    boundary and documentation checks
├─ CMakeLists.txt  CMakePresets.json
├─ VERSION  CHANGELOG.md  LICENSE  THIRD_PARTY_NOTICES.md  README.md
└─ openstrata.toml  openstrata.ci.yaml
```

A component's own unit tests live in its `tests/`; the root `tests/` holds only
what spans components. Fixture generators live in `tests/fixtures/`; the
fixtures a bundle's verification pyramid opens are committed inside the bundle,
because `ost` requires a bundle's test paths to stay inside it.

## 4. Manifests, versioning and build metadata

- **`VERSION`** at the repository root is the single product version. The git
  tag (`vX.Y.Z`), `CHANGELOG.md`, and any manifest that must carry a version
  for a standalone build mirror it; nothing else defines one.
- **`openstrata.toml`** declares the workspace; **`openstrata.ci.yaml`** is the
  CI support matrix from which workflows are generated. Generated workflows are
  not hand-edited.
- Each component carries its manifest beside it: `openstrata.plugin.yaml` for
  a bundle, `openstrata.library.yaml` for a library, `openstrata.tool.yaml` for
  an executable.
- The bundle manifest provides `usd-fileformat:blend`, declares the license as
  `Apache-2.0`, and states the OpenUSD runtime this repository pins
  ([DEPENDENCIES.md §1](DEPENDENCIES.md#1-openusd)) — not the template's
  scaffold default.
- The **stage-contract version** is separate from the product version and
  changes only under
  [STAGE_CONTRACT.md §2](../design/STAGE_CONTRACT.md#2-contract-version).

Intended bundle manifest, before it is generated and adjusted:

```yaml
plugin:
  name: usdBlendFileFormat
  version: 0.1.0
  kind: usd-fileformat
license: Apache-2.0
runtime:
  openusd: "<the pinned release>"
provides:
  - usd-fileformat:blend
requires:
  capabilities:
    - usd-stage-read
usd:
  plug_info: plugin/resources/usdBlendFileFormat/plugInfo.json
```

## 5. Build modes

One CMake contract, driven two ways. The difference is only who prepares the
dependency prefix — the installed OpenUSD and compression libraries — never
what the CMake does with it:

```sh
# Plain CMake: the prefix is the caller's
cmake -S . -B build -DCMAKE_PREFIX_PATH=<dependency-prefix>
cmake --build build --config Release
ctest --test-dir build -C Release

# OpenStrata: ost composes the prefix and configures the same files
ost plugin inspect plugins/usdBlendFileFormat
ost plugin build   plugins/usdBlendFileFormat
ost plugin doctor  plugins/usdBlendFileFormat
ost plugin test    plugins/usdBlendFileFormat
```

Exact commands are in [the build guide](../guides/building.md). The `reader`
preset builds and tests `blendFile` without finding OpenUSD.

Rules every `CMakeLists.txt` keeps:

| Rule | Detail |
| --- | --- |
| In-repository edges | the in-tree target when it exists, the installed package otherwise, so a component configures composed by the root and on its own |
| External edges | `find_package(<package> CONFIG REQUIRED)` on an installed package; never a sibling checkout or a probe for one |
| OpenUSD | resolved once at the root, **after** `blendFile`, `blendScene` and `blend_inspect` are added, so nothing they configure can see `pxr` |
| Build settings | per target, never per directory |

- **Windows:** every target compiles with `/utf-8` and `NOMINMAX`; executables
  embed a UTF-8 active-code-page manifest.

## 6. Tests

These are test responsibilities and intended locations; fixture-backed support
is recorded in the [capability matrix](../reference/CAPABILITY_MATRIX.md).

| Layer | Where | Proves |
| --- | --- | --- |
| header, byte sources, container and SDNA | `libs/blendFile/tests/` | levels 1–2 of [DESIGN_POLICY.md §13](../design/DESIGN_POLICY.md#13-testing-policy) |
| Scene IR | `libs/blendScene/tests/` | level 3, against expected IR |
| authoring | `plugins/usdBlendFileFormat/tests/` | level 4, from IR built in code |
| boundary | each library's `tests/` | §2.3's link-line and include gates |
| tool | `tools/blendInspect/tests/` | `blend_inspect` against fixtures |
| integration | `tests/integration/` | `Usd.Stage.Open("*.blend")` against [STAGE_CONTRACT.md §17](../design/STAGE_CONTRACT.md#17-validation-checklist) |
| pyramid | the bundle manifest's `tests:` | `ost plugin test` L0–L5 |
| installed consumer | `tests/installed_consumer/` | installed packages work from a clean prefix |
| oracle | `tests/integration/` | native IR equals Blender's ([BACKEND_POLICY.md §6](../design/BACKEND_POLICY.md#6-blender-as-a-test-oracle)) |
| fuzz | `libs/blendFile/fuzz/` | malformed input never crashes or over-reads, under ASan and UBSan |

`plugins/usdBlendFileFormat/tests/test_stage.py` checks the minimal stage,
diagnostic codes, deterministic reads and reference composition in the
OpenStrata runtime session.

## 7. Invariants

Every change preserves these; a change that cannot, changes this document
first.

1. The dependency edges are those of §2, declared in manifests and gated in CI.
2. `blendFile` and `blendScene` link no OpenUSD.
3. No component links Blender.
4. The file format reads only; it never writes `.blend`.
5. The same bytes produce the same stage.
6. The authored stage does not change meaning without a stage-contract bump.
7. Every build mode of §5 works, and every component builds against installed
   siblings.
8. A capability is claimed only with a fixture behind it
   ([CAPABILITY_MATRIX.md](../reference/CAPABILITY_MATRIX.md)).

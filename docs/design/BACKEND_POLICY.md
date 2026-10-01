# Backend policy

> Status: **proposed**, 2026-10-01. Nothing here is implemented yet; the
> native backend grows from Phase 1, the Blender host backend is Phase 7.
>
> This document owns the backends that produce a Scene IR, the boundary to
> Blender itself, and the difference between source and evaluated data. On
> this area it wins over the design policy.

## 1. Scope

The final goal is a **native `.blend` reader** that needs no Blender. Matching
Blender's own compatibility from the first day is too heavy, so the reader is
behind a backend interface, and Blender itself can stand behind the same
interface as a bootstrap, an oracle and an evaluated-geometry fallback.

## 2. The backend interface

```cpp
namespace blend {
class IBlendBackend {
public:
    virtual ~IBlendBackend() = default;
    virtual Result<Scene> Read(ByteSource& source,
                               const ReadOptions& options) = 0;
};
}
```

Every backend produces the same Scene IR, already in the USD basis, with
identifiers assigned ([DESIGN_POLICY.md §5.2](DESIGN_POLICY.md#52-blendscene--the-scene-ir-and-native-decoding)).
USD authoring cannot tell which backend ran, except through provenance.

| Backend | Component | Role | Default |
| --- | --- | --- | --- |
| `NativeBlendBackend` | `blendScene` over `blendFile` | the standard path | yes |
| `BlenderHostBackend` | `blendHost` | bootstrap, oracle, evaluated data | never |

## 3. The native backend

- needs no Blender installation;
- runs entirely inside the OpenUSD process;
- is fast, and usable on servers and headless machines;
- reads through `ByteSource`, so it can later read from an `ArAsset` served by
  any resolver ([BLEND_CONTRACT.md §3](BLEND_CONTRACT.md#3-byte-sources)).

It reads **source data** ([BLEND_CONTRACT.md §11](BLEND_CONTRACT.md#11-what-is-not-read)).
SDNA makes a `.blend` self-describing, so an independent parser can read its
raw serialization. Blender's versioning code and its evaluated scene are not
the same thing as that serialization, and the difference is handled
explicitly (§5).

## 4. The Blender host backend

Blender runs as a **separate process**; the plugin never links it.

```text
usdview
  └─ usdBlendFileFormat
       └─ blendHost ── subprocess ──→ blender --background ... (separately installed)
                     ←── Scene IR ───
```

- Blender is found only through explicit configuration (BACK-O2). The file
  format does not search the system for a Blender installation.
- A Python script committed in this repository runs inside Blender, reads the
  scene through Blender's API, and writes a Scene IR in an interchange format
  (BACK-O1) that `blendHost` validates like any other external input.
- It is costly — process start-up, Blender's own load time — so it is never
  the normal path, and the native backend is never replaced by it.

Uses:

- bootstrapping a concept before the native decoder reads it;
- the reference for native decoding (§6);
- evaluated geometry: modifiers and Geometry Nodes (§5);
- a compatibility fallback for files the native reader cannot read yet.

## 5. Source and evaluated representations

```text
native backend         → source representation (stored data)
Blender host backend   → source or evaluated representation (Blender's depsgraph)
```

Evaluated data — the modifier stack, Geometry Nodes, constraints, drivers,
simulation — is Blender's dependency graph. Re-implementing it is a non-goal
([DESIGN_POLICY.md §17](DESIGN_POLICY.md#17-non-goals-for-the-first-releases)).

Whether a user can choose the representation for one asset, for example with
file-format arguments (`model.blend:SDF_FORMAT_ARGS:evaluation=blender`), is
BACK-O4. The first releases add no such setting.

## 6. Blender as a test oracle

The native backend's correctness is checked against Blender itself:

```text
fixture.blend
   ├─ NativeBlendBackend ─→ Scene IR A
   └─ BlenderHostBackend ─→ Scene IR B   (source representation)
                            compare A and B
```

Compared: hierarchy, transforms, topology, normals, UVs, material parameters.
This catches parser drift between Blender versions. Oracle tests run where a
Blender executable is configured — locally and in a dedicated CI lane — and
are skipped elsewhere. Blender is never a runtime dependency of the plugin.

Until `blendHost` exists (Phase 7), fixtures carry the expected IR as JSON
written by the same committed Blender script when the fixture was generated
([DESIGN_POLICY.md §13](DESIGN_POLICY.md#13-testing-policy)).

## 7. License boundary

The project is Apache-2.0. Blender is GPL-licensed. Therefore:

```text
Apache-2.0 usd-blender-plugins
        │  process invocation, never linking
        ▼
separately installed Blender
```

- No component links Blender or any Blender library.
- The native reader is written independently, from Blender's public developer
  documentation and observed file behavior. Blender source code is not copied
  into this repository.
- No Blender binary is distributed with this repository's packages.
- The in-Blender script's license, and every dependency's, is checked before
  the first release that ships `blendHost` (BACK-O3).

## 8. Security of the host backend

A `.blend` can carry Python scripts that Blender runs on load. The host backend
treats every `.blend` as untrusted:

- auto-run of scripts in the file is always disabled (`--disable-autoexec`);
- Blender starts with factory settings (`--factory-startup`), so no user
  add-on or startup file runs;
- Blender is started with an argument vector, never through a shell, and the
  asset path is never interpolated into a command string;
- every run has a timeout, and a killed or failed process is a diagnostic;
- interchange output is written to a private temporary location, validated
  with the reading rules of [BLEND_CONTRACT.md §2](BLEND_CONTRACT.md#2-reading-rules),
  and removed afterwards.

## 9. Open questions

| Id | Question | Proposed answer | Blocks |
| --- | --- | --- | --- |
| BACK-O1 | The Scene IR interchange format between Blender and `blendHost`. | A versioned JSON header with binary arrays, the same one fixtures use for expected IR. | Phase 7 |
| BACK-O2 | How a Blender executable is configured. | An environment variable read by `blendHost`, and the CI lane's tool configuration; never auto-discovery. | Phase 7 |
| BACK-O3 | The license of the script that runs inside Blender, and of packages that ship it. | Decide with the release that first ships `blendHost`; ship it separately if needed. | the first release with `blendHost` |
| BACK-O4 | Whether evaluation mode is exposed as file-format arguments. | Not in the first releases; revisit with a consumer that needs evaluated geometry. | nothing (non-blocking) |

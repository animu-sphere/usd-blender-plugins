# OpenUSD Blender Plugins

OpenUSD plugins for [Blender](https://www.blender.org/) `.blend` files: a
read-only `SdfFileFormat` that opens a `.blend` directly as a USD layer.

Implemented behavior is recorded in the
[capability matrix](docs/reference/CAPABILITY_MATRIX.md); phase status and
upcoming work are in [the roadmap](docs/roadmap/README.md#status-at-a-glance).
The current [Blender 5.x support scope](docs/design/BLEND_CONTRACT.md#9-version-support)
defers full older-version compatibility to the later roadmap.

The target scene workflow:

```usda
#usda 1.0

def Xform "Scene" (
    references = @character.blend@
)
{
}
```

```sh
usdview scene.blend
```

## The central rule

> Read the data a `.blend` stores, normalize it once into a Blender-neutral
> scene, author conventional OpenUSD, and keep Blender's evaluation outside the
> file format.

```text
.blend bytes ─→ blendFile ─→ blendScene ─→ usdBlendFileFormat ─→ USD stage
                container,   Scene IR      SdfFileFormat
                SDNA         (no OpenUSD)
                (no OpenUSD)
```

Blender is never linked. A native reader is the normal path and needs no
Blender installation; a separately installed Blender can be used, across a
process boundary, as a reference oracle and for evaluated geometry. Writing
`.blend` is out of scope.

## Components

| Component | Kind | Role |
| --- | --- | --- |
| `blendFile` | plain C++ library | bounded `.blend` syntax: byte sources, diagnostics, compression, headers, blocks and SDNA — no OpenUSD |
| `blendScene` | plain C++ library | the Blender-neutral Scene IR and the native decoder — no OpenUSD |
| `usdBlendFileFormat` | OpenUSD `SdfFileFormat` bundle | resolver-backed input and USD authoring through the file-format boundary |
| `blend_inspect` | CLI | what a `.blend` contains, without USD |
| `blendHost` | plain C++ library (later) | the Blender host backend, over a subprocess |

Identities and dependency directions are fixed in
[docs/architecture/WORKSPACE.md](docs/architecture/WORKSPACE.md).

## What the importer will author

```text
/Asset                 UsdGeomXform, defaultPrim; Y-up, meters
  geo/<Object>/mesh    UsdGeomXform per object, UsdGeomMesh per mesh
  mtl/<Material>       UsdShadeMaterial with a UsdPreviewSurface realization
  skel/                UsdSkelSkeleton per armature
  cameras/             UsdGeomCamera
  lights/              UsdLux lights
  collections          UsdCollectionAPI per Blender collection
```

The same `/Asset` contract as `usd-mmd-plugins` and `usd-vrm-plugins`, so
stages from each compose without special cases. The full contract is
[docs/design/STAGE_CONTRACT.md](docs/design/STAGE_CONTRACT.md).

## Documentation

Build and test commands: [docs/guides/building.md](docs/guides/building.md).

| | |
| --- | --- |
| [docs/design/](docs/design/) | What the file format reads and authors, and why — start with [DESIGN_POLICY.md](docs/design/DESIGN_POLICY.md) |
| [docs/architecture/](docs/architecture/) | The binding workspace contract and external dependencies |
| [docs/reference/](docs/reference/) | What is implemented, and diagnostics |
| [docs/roadmap/](docs/roadmap/) | Phase status, release mapping and incomplete tasks |
| [docs/contributing/](docs/contributing/) | How the documentation is maintained |

Changes are recorded in the [changelog](CHANGELOG.md).

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) for development and review expectations,
[CODE_OF_CONDUCT.md](CODE_OF_CONDUCT.md) for community standards, and
[SECURITY.md](SECURITY.md) for private vulnerability reporting.

## License

[Apache-2.0](LICENSE). Third-party code retains its own license; see
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
Blender is a separate, GPL-licensed program; this repository
neither contains nor links it
([docs/design/BACKEND_POLICY.md §7](docs/design/BACKEND_POLICY.md#7-license-boundary)).

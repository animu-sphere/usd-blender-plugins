# usd-blender-plugins — design policy

> Status: **proposed** as the project's design policy, 2026-10-01. This document
> defines intended behavior. The
> [capability matrix](../reference/CAPABILITY_MATRIX.md) owns implemented
> behavior; the [roadmap](../roadmap/README.md#status-at-a-glance) owns phase
> status.
>
> This is the canonical, long-form policy: why the project is shaped the way it
> is, where its boundaries are, and the order it is built in. It is distilled
> from the 2026-10-01 implementation plan (v2). Five focused documents own the
> detail of one area each, and **on its own area the focused document wins**:
>
> | Area | Owning document |
> | --- | --- |
> | The authored USD stage | [STAGE_CONTRACT.md](STAGE_CONTRACT.md) |
> | `.blend` bytes → Scene IR | [BLEND_CONTRACT.md](BLEND_CONTRACT.md) |
> | Materials, textures and images | [MATERIAL_POLICY.md](MATERIAL_POLICY.md) |
> | Names, identifiers and paths | [NAMING_POLICY.md](NAMING_POLICY.md) |
> | Native reader, Blender host backend, evaluation | [BACKEND_POLICY.md](BACKEND_POLICY.md) |
> | Component identities and dependency edges | [architecture/WORKSPACE.md](../architecture/WORKSPACE.md) |
>
> Section numbers are stable so other documents can cite them ("design policy
> §7"). A later revision may add subsections; a numbered section never changes
> meaning.

---

## 1. Purpose

`usd-blender-plugins` lets OpenUSD read Blender `.blend` files directly, as
ordinary layers, through a **read-only `SdfFileFormat` plugin**:

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

To OpenUSD a `.blend` is a normal `SdfLayer`; inside, Blender data is converted
to standard USD schemas. The whole policy reduces to one rule:

> **Read the data a `.blend` stores, normalize it once into a Blender-neutral
> scene, author conventional OpenUSD, and keep Blender's evaluation outside the
> file format.**

The first implementation target is the **reader**. Writing `.blend` is out of
scope (§2.4). The long-term goal is a native reader that needs no Blender
installation; a process-isolated Blender backend exists as a bootstrap,
reference oracle and evaluated-geometry fallback, never as the normal path
([BACKEND_POLICY.md](BACKEND_POLICY.md)).

It is not a Blender add-on, an exporter that runs inside Blender, a Blender
runtime replacement, or a renderer.

## 2. Design principles

### 2.1 OpenUSD first

Standard schemas carry everything they can carry faithfully enough:
`UsdGeomXform`, `UsdGeomScope`, `UsdGeomMesh`, `UsdGeomSubset`,
`UsdGeomBasisCurves`, `UsdGeomPoints`, `UsdGeomCamera`, `UsdLux*`,
`UsdShadeMaterial`, `UsdShadeShader`, `UsdSkelSkeleton`, `UsdSkelAnimation`,
`UsdSkelBindingAPI` and `UsdCollectionAPI`.

No Blender-specific schema is created in the first releases. One is considered
only through the admission test (§6). Blender node types are never mirrored as
USD schemas.

### 2.2 A thin file-format boundary

`usdBlendFileFormat` is the glue between OpenUSD and the reader. It owns:

- `.blend` registration and identification;
- receiving the resolved asset and starting a backend;
- translating reader diagnostics into OpenUSD errors and warnings;
- calling USD authoring on the Scene IR and handing the result to `SdfLayer`.

It does **not** own DNA parsing, mesh decoding, material conversion, Blender
version handling, texture search or modifier evaluation. Each of those lives in
a component with its own tests (§5).

```cpp
// Concept only; the intended boundary is §4.
bool UsdBlendFileFormat::Read(SdfLayer* layer,
                              const std::string& resolvedPath,
                              bool metadataOnly) const
{
    blend::ReadOptions options;
    options.metadataOnly = metadataOnly;

    blend::Scene scene;
    blend::Diagnostics diagnostics;
    if (!blend::ReadAsset(resolvedPath, options, &scene, &diagnostics)) {
        // Fatal diagnostics become TF_RUNTIME_ERROR; nothing is authored.
        return false;
    }
    return AuthorLayer(scene, diagnostics, layer);
}
```

### 2.3 Source data, not evaluated data

The native reader reads the **source data a `.blend` stores**. The modifier
stack, Geometry Nodes, constraints, drivers and simulation are Blender's
dependency graph; evaluating them with Blender's meaning is a different layer
from parsing a file. The default path authors the source representation, and
says so with a diagnostic when a source object carries evaluation it does not
apply. An evaluated representation comes only from the Blender host backend
([BACKEND_POLICY.md §5](BACKEND_POLICY.md#5-source-and-evaluated-representations)).

### 2.4 Reader first

The scope is `.blend` → USD. `SdfLayer::Export("foo.blend")` is not
implemented: a writer faces version compatibility, SDNA, Blender semantics,
datablock ownership and the dependency graph, which is a separate project.
`WriteToString()` serializes the authored layer as USD text for debugging.

### 2.5 Determinism

The same `.blend` bytes and the same reader version author the same stage.
Nothing depends on the process locale, the filesystem encoding, the time of
day, hash-map iteration order, or the order a backend happens to visit
datablocks. Concretely: deterministic identifiers
([NAMING_POLICY.md](NAMING_POLICY.md)), one explicit coordinate conversion
([STAGE_CONTRACT.md §6](STAGE_CONTRACT.md#6-coordinate-conversion)), and a
stamped stage-contract version
([STAGE_CONTRACT.md §2](STAGE_CONTRACT.md#2-contract-version)). Reference,
override and cache stability depend on it.

### 2.6 Format syntax, scene semantics and authoring are different layers

```text
.blend bytes ─→ blendFile ─→ blendScene ─→ usdBlendFileFormat ─→ SdfLayer
               container,    Scene IR:     USD authoring
               SDNA, blocks  Blender-neutral
```

Blender's internal structures never reach USD authoring, and USD schema
details never reach the parser. Each arrow is a function with its own tests
(§4, §13).

### 2.7 Independent of Blender

Blender is never linked into any component. The native reader is written from
the format's documented and observable behavior, not from Blender source code.
Where Blender itself is used — the host backend, the test oracle, fixture
generation — it is a separately installed executable behind a process boundary
([BACKEND_POLICY.md §7](BACKEND_POLICY.md#7-license-boundary)).

## 3. Relationship to sibling repositories

Contributors who know `usd-mmd-plugins` or `usd-vrm-plugins` should find this
repository familiar. What carries over is **architecture**, not internals:

- a repository-level CMake workspace, with `plugins/` for OpenUSD plugin
  bundles, `libs/` for plain C++ libraries and `tools/` for executables;
- parser code separate from USD authoring, and no OpenUSD in the parser;
- components that build, test and package independently, each with its
  OpenStrata manifest beside it;
- plain CMake support alongside `ost`;
- the same documentation taxonomy (architecture · design · guides · reference ·
  roadmap · releases · reports · contributing);
- the `/Asset`-rooted stage with `defaultPrim = "Asset"`, `geo` and `mtl`
  scopes, Y-up and meters, so that the stages compose with the siblings'
  without special cases
  ([STAGE_CONTRACT.md §4](STAGE_CONTRACT.md#4-prim-hierarchy)).

What deliberately does **not** carry over:

- **No humanoid or avatar semantics.** A `.blend` is a general scene, not an
  avatar format.
- **No package resolver in the first releases.** Packed resources are a
  resolver concern and come later (§9).
- **No schema bundle.** Nothing has passed the admission test (§6).

## 4. Pipeline and testable transitions

```text
ByteSource ──Open──→ blend::File ──Decode──→ blend::Scene ──Author──→ SdfLayer
 (bytes)          (container, SDNA,      (Scene IR)
                   blocks, ID graph)
```

The three transitions are separate functions in separate components, so each
is testable without the next:

```cpp
// blendFile — no OpenUSD
namespace blend {
class ByteSource {
public:
    virtual ~ByteSource() = default;
    virtual uint64_t Size() const = 0;
    virtual bool Read(uint64_t offset, std::span<std::byte> dst) = 0;
};

class File;  // header, blocks, SDNA, pointer map
Result<File> OpenFile(ByteSource& source, const OpenOptions& options);
}

// blendScene — no OpenUSD
namespace blend {
struct ReadOptions {
    bool metadataOnly = false;
    bool readGeometry = true;
    bool readMaterials = true;
    bool readAnimation = true;
};

struct Scene;  // objects, meshes, materials, images, cameras, lights, ...
Result<Scene> Decode(const File& file, const ReadOptions& options);

class IBlendBackend {
public:
    virtual ~IBlendBackend() = default;
    virtual Result<Scene> Read(ByteSource& source,
                               const ReadOptions& options) = 0;
};
}

// usdBlendFileFormat (internal)
bool AuthorLayer(const blend::Scene& scene,
                 const blend::Diagnostics& diagnostics,
                 SdfLayer* layer);
```

`Result<T>` carries either a value and its recoverable diagnostics, or the
fatal diagnostic that prevented one
([reference/DIAGNOSTICS.md](../reference/DIAGNOSTICS.md)). Signatures above are
the intended public boundary, not a frozen ABI; §15 lists what *is* frozen.

## 5. Component responsibilities

Identities, directories, kinds and permitted edges are fixed in
[WORKSPACE.md](../architecture/WORKSPACE.md). This section says what each one
is *for*.

### 5.1 `blendFile` — container and SDNA reader

A plain C++ library with **no OpenUSD dependency**. It owns `ByteSource`,
decompression, the file header, the legacy and Blender 5 block layouts, pointer
size and endianness, the DNA1 block and SDNA, raw datablock access by struct
and member name, and the old-pointer map. It exposes **source facts, not
scene policy**: a block has a code, a size, an SDNA struct and an address,
never a `pxr::SdfPath` and never an `Object`
([BLEND_CONTRACT.md](BLEND_CONTRACT.md)).

The [borrowed SDNA value boundary](BLEND_CONTRACT.md#83-borrowed-sdna-value-boundary)
exposes bounded structure/member/array storage, saved pointers and typed
scalars. It does not select a Scene, follow references or convert values; the
semantic decoder owns those decisions and copies results into the owning IR.

### 5.2 `blendScene` — the Scene IR and native decoding

A plain C++ library with **no OpenUSD dependency**. It owns the Scene IR —
the Blender-neutral representation every backend produces — and the native
decoder that builds it from a `blend::File`. It absorbs Blender version
differences, reconstructs the ID graph, and applies **the single
Blender-to-USD coordinate and unit conversion**
([STAGE_CONTRACT.md §6](STAGE_CONTRACT.md#6-coordinate-conversion)), and owns
identifier generation ([NAMING_POLICY.md](NAMING_POLICY.md)). After decoding
geometric values are in the USD basis and distances are meters; nothing
downstream flips an axis or applies the source unit scale again.

The Scene IR is the most important abstraction in the project: it keeps
Blender binary details and USD schema details from ever meeting.

```cpp
namespace blend {
struct Scene {
    SceneMetadata metadata;   // Blender version, frame range, units, provenance
    std::vector<Object> objects;
    std::vector<Mesh> meshes;
    std::vector<Material> materials;
    std::vector<Image> images;
    std::vector<Camera> cameras;
    std::vector<Light> lights;
    // Later Phases add collections, armatures and animation.
};
}
```

### 5.2.1 Scene IR foundation

The initial public types live in `blendScene/Scene.h`. They are owning values:
`SceneMetadata`, `Object`, `Mesh` and `UvMap`, using only standard C++ types.
`Scene` contains object and mesh vectors; later data categories are added when
their decoders exist. An object's optional `parent` and `mesh` are indices into
those vectors, not saved Blender pointers. Several objects may refer to the
same mesh. No parent means a root object, and no mesh means an empty object in
this initial scope. Source names and assigned identifiers remain separate.

`Vector2` and `Vector3` use doubles. `Matrix4` is row-major storage with
column-vector mathematics: translation occupies `[0][3]`, `[1][3]`, `[2][3]`,
and a child world matrix is `parentWorld * childLocal`. Object matrices default
to identity. Objects carry world matrices in the USD basis; authoring must
derive parent-relative matrices and transpose for USD's row-vector convention,
not reinterpret the stored elements as an OpenUSD matrix.

`ToUsdBasis(Vector3)` performs `(x, y, z) -> (x, z, -y)` without unit scaling.
Use it for directions and normals; it does not normalize normal lengths.
`ToUsdBasis(Matrix4)` performs `C * W * inverse(C)` without unit scaling.
These low-level basis helpers preserve lengths and right-handed winding.

`UnitConversion` is explicitly constructed from a finite, strictly positive
source `scale_length`, with no default or corrupt-value fallback. Its private
factor is exposed read-only by `MetersPerBlenderUnit()`. `Distance` converts a
stored distance to meters. `Position` calls `Distance` per component, then
applies `ToUsdBasis`. `WorldTransform` accepts an affine mesh/empty matrix,
scales only its translation, then applies the basis conjugation. Rotation,
shear and dimensionless object scale are preserved apart from basis rotation.
Create one conversion for the selected Scene and pass it to semantic decoders;
these APIs accept source-space values, not values already in the IR. Do not
call them on normalized values during USD authoring.

Invalid scale, nonfinite distance, non-affine transform and conversion overflow
throw standard C++ exceptions prefixed by stable `BLEND_SCENE_UNIT_*` codes
([diagnostics](../reference/DIAGNOSTICS.md#3-implemented-codes)). A future native
decoder must translate them into fatal scene diagnostics with source context
and publish no affected Scene. The helper validates unit-specific inputs, not
all matrix or mesh data. It does not implement the separate camera/light
matrix convention, derive local transforms, select a saved Scene or resolve
STAGE-O1's Blender-written evidence requirement. `sourceUnitScale` retains
source provenance; it never changes the downstream interpretation of IR.

Meshes carry meter-space points, face counts, corner vertex indices,
face-varying corner normals, and named indexed UV maps with an active-render
flag. Object world translations are also meters. Normals, UVs and topology
indices are not unit-scaled. The initial IR
is a value container, not a validator: decoders must validate references,
cycles, topology and array sizes before publishing a Scene. This boundary does
not introduce a native `Decode` or backend implementation. Evidence belongs in
the [capability matrix](../reference/CAPABILITY_MATRIX.md#5-scene-ir).

### 5.2.2 Saved-scene selection boundary

`SelectScene(bytes, blocks, schema, header)` in `blendScene/Selection.h`
consumes the same caller-validated uncompressed bytes, ordered block records,
SDNA schema and header used by the reader. It returns `Result<SelectedScene>`:
the selected zero-based `blockIndex` and owning `SceneMetadata`, not a populated
object/mesh IR. The index belongs to the supplied block sequence; metadata
strings remain valid after inputs are released. Full-file and block budgets
remain the caller's responsibility. Selection neither opens another file nor
finds or verifies the file's DNA1 block count.

Exactly one `GLOB` containing one `FileGlobal` is required. Its scalar
`Scene *curscene` must resolve through the exact-key pointer map to one `SC`
block with `Scene` SDNA type. Missing, null, absent/interior, wrong-type and
multi-element references fail without choosing the first or only Scene.
In particular, a Scene-only library with null `curscene` has no implicit
fallback. Pointer-map duplicate validation still applies to all reference
targets, not just the selected Scene.

The selected Scene must contain scalar embedded `ID` and `UnitSettings`
members. A nonzero scalar `Library *ID.lib` is reported as a fatal
`BLEND_SCENE_LINKED_UNSUPPORTED`, not resolved or loaded. `ID.name` must be
a terminated one-dimensional, one-byte `char` array with the `SC` prefix;
`sourceScene` copies the bytes after that prefix. This is raw source provenance,
not UTF-8 validation, normalization or an identifier policy. `sourceVersion`
comes from the supplied header. `Scene.unit.scale_length` must be finite and
strictly positive and is retained as `sourceUnitScale`, not applied here.

Semantic errors are fatal, non-recoverable `BLEND_SCENE_*` diagnostics with
the referring GLOB or selected target's uncompressed payload offset and block
index. A missing GLOB or allocation failure has no block context. Reader
`BLEND_DNA_*`, `BLEND_BLOCK_*` and pointer-map errors retain their original
context. Other scenes are not semantically decoded. No Collection/ListBase
walk, object graph, unit-normalized geometry, backend `Decode`, Scene IR
publication or USD authoring is introduced by selection. Fixture-backed scope
is in the [capability matrix](../reference/CAPABILITY_MATRIX.md#5-scene-ir).

### 5.2.3 Saved Collection membership boundary

`SelectSceneObjects(bytes, blocks, schema, header, limits)` in
`blendScene/Selection.h` first applies the saved-scene selection policy above,
then walks `Scene.master_collection`. It returns `Result<SelectedSceneObjects>`:
the owning `SelectedScene` and a vector of `SelectedObject` records containing
caller-sequence `blockIndex` values and owning, prefix-stripped raw `sourceName`
bytes, plus optional `parentBlockIndex` values validated under the
[parent-reference boundary](#524-saved-object-parent-reference-boundary)
and optional `dataBlockIndex` values under the
[data-reference boundary](#525-saved-object-data-reference-boundary).
It does not publish an object/mesh Scene IR. Input matching, full-file,
block and schema budgets remain the caller's responsibility; the pointer map
still validates duplicate saved addresses across the entire supplied sequence.

Collections must resolve exactly to one `Collection` with `GR` or `DATA` code,
including the saved master Collection. Their embedded scalar `ListBase`
members `gobject` and `children` use scalar `void *first/last`. Every list node
must be one `DATA` record with the corresponding `CollectionObject` or
`CollectionChild` SDNA type, typed scalar `next/prev` pointers and a typed
scalar `ob` or `collection` target. Object targets must be one `OB` / `Object`.
Null, absent, interior, wrong-type and multi-element required targets fail;
empty lists require both endpoints to be null. Backlinks must match the
previous node, and `last` must identify exactly the node with null `next`.
A node shared by different lists is invalid. Collection and Object IDs require
embedded scalar `ID`, null scalar `Library *lib` and terminated `GR`/`OB`
names; linked IDs fail without accessing external files.

The walk is iterative, depth-first in saved child-list order, inspecting each
Collection's object list before its children. An Object shared by multiple
Collections appears once at first discovery. Completed Collections can be
shared; references to active Collections and repeated nodes in a next chain
fail with `BLEND_SCENE_CYCLE`. Discovery order and source names are independent
of block enumeration, not a deterministic identifier or sibling-name policy.

Both `SceneTraversalLimits` fields are required and positive; zero-initialized
limits are invalid. `maxVisited` counts each distinct expanded Collection,
each traversed list node and each distinct Object, including parent-only
Objects, plus distinct data targets under the data-reference boundary,
excluding Scene/GLOB selection and pointer-map construction. `maxDepth` bounds
the active DFS Collection stack and the separate active parent chain,
with the master at depth one, not the longest path through a shared DAG.
Exactly sufficient budgets succeed; exceeding them returns fatal
`BLEND_SCENE_VISIT_LIMIT` or `BLEND_SCENE_DEPTH_LIMIT`, never partial output.
No production default is introduced for these or decompression budgets.

Semantic diagnostics attach the referring or invalid target block's payload
offset and index. Unresolved list heads use Collection context; unresolved
`next` pointers use the previous list node. Collection cycles/depth limits
identify the referring Collection. Invalid limits and allocation failures
have no source context. Reader failures retain their existing codes/context.
Records outside the selected Scene, membership, parent chains and immediate
data targets are not semantically inspected.
This is saved membership, not evaluated/view-layer/render visibility: data
references are separately validated below; instance-Collection references,
transforms, geometry and object types are not decoded or validated.
Backend `Decode`, populated Scene IR and USD
integration remain separate work. Fixture-backed scope is in the
[capability matrix](../reference/CAPABILITY_MATRIX.md#5-scene-ir).

### 5.2.4 Saved Object parent-reference boundary

After Collection membership succeeds, `SelectSceneObjects` validates every
selected Object's scalar `Object *parent` and its reachable parent chain.
A null pointer is a valid root; every nonzero pointer must resolve exactly to
one `OB` block with `Object` SDNA type. Each reached Object must have the same
local, valid `ID` and raw name shape required by membership. Missing members
and malformed SDNA retain reader diagnostics; absent/interior, wrong-code,
wrong-type and multi-element targets fail with `BLEND_SCENE_REFERENCE_INVALID`.
Linked parent IDs fail without resolving or loading external files.

`SelectedObject.parentBlockIndex` is the optional immediate parent's index in
the caller's block sequence, **not** an index into the returned Object vector
or Scene IR. A parent outside the selected Collections is validated but never
added to membership. No ancestor names or values are published. Shared parents
are valid, and null roots are not replaced by Collection hierarchy.

Parent validation is iterative in saved Object discovery order. A reference
to an active ancestor fails with `BLEND_SCENE_CYCLE`; completed chains are
cached and not expanded again. `maxVisited` counts the union of membership
Objects and parent-only Objects once each, in addition to Collections/list
nodes. `maxDepth` separately bounds the number of unfinished Objects in the
active parent chain, with its selected starting Object at depth one. Reaching
a completed chain stops expansion; this is an active-work bound, not a bound
on the longest full parent path. Collection and parent depths are not summed.
Exactly sufficient limits succeed; invalid limits or exhaustion return fatal
errors without partial output, using no production defaults.

Unresolved parents use the referring Object's payload offset and block index;
invalid targets use target context. Cycle, parent-depth and parent-only visit
failures identify the referring Object. Existing selection/membership/reader
errors are preserved. Unreachable Objects unrelated to membership or its
ancestors or immediate data targets are not semantically validated.
This validates saved parent edges
only: parenting mode, bone/vertex targets, parent inverse, local/world matrices,
render visibility, instance references and IR/USD hierarchy authoring
remain separate work. Both normal-save corpus files provide null-parent
evidence and mutated-pointer regressions, not Blender-written nontrivial
parenting or transform-oracle evidence; nontrivial chains use synthetic inputs.

### 5.2.5 Saved Object data-reference boundary

During parent-chain validation, `SelectSceneObjects` also checks every selected
or parent-only Object's scalar `data` pointer. Its SDNA declaration must be
`void *` or `ID *`; both are accepted based on the stored schema rather than
host layout. A null value is retained as no target. Every nonzero value must
resolve exactly to one ID block: two uppercase ASCII letters followed by two
NUL bytes, count one, a valid SDNA value containing scalar embedded `ID`,
null scalar `Library *ID.lib` and a terminated one-dimensional one-byte
`char` name whose prefix matches that target's block code. Linked data fails
without loading external files. Reader binding/member errors are preserved.

`SelectedObject.dataBlockIndex` is the optional immediate data target's index
in the caller's block sequence, not a selected Object or Scene IR index.
Parent-only data targets are validated but not published as membership.
No data names or geometry are published. Shared targets are validated once;
`maxVisited` counts the union of reached Objects and data targets once each,
in addition to expanded Collections and list nodes. Data targets do not extend
the parent-depth bound. Exactly sufficient budgets succeed without production
defaults. Missing/interior addresses and data visit exhaustion use referring
Object context; invalid or linked targets use target context. Invalid shapes
fail with `BLEND_SCENE_REFERENCE_INVALID`, with no partial selection.

This is generic ID-edge validation, not `Object.type` decoding or enforcement
of a type-to-data mapping. It permits null even for a stored mesh Object and
does not reject an otherwise valid local ID solely because that Object kind
would require another data type. Data-internal references are not followed;
data cycles, instance Collections, Mesh storage and geometry, Object values,
populated Scene IR and USD authoring remain separate work. The importer and
inspection tool do not consume this selection API. Fixture-backed scope is in
the [capability matrix](../reference/CAPABILITY_MATRIX.md#5-scene-ir).

### 5.2.6 Saved Object value boundary

`SelectSceneObjectValues(bytes, blocks, schema, header, limits)` shares the
selection and traversal above, then validates values for every selected and
parent-only Object. It returns the same `SelectedSceneObjects`, with an owning
`SavedObjectValues` in each selected record's optional `values`. The generic
`SelectSceneObjects` leaves `values` absent and retains its nullable,
type-independent data policy. Parent-only values are checked but not published.

`Object.type` and `transflag` must be scalar two-byte `short` values. Visibility
uses the saved `visibility_flag` or `restrictflag` member, a scalar two-byte
`short` or four-byte `int`. Only bit 2 controls `hiddenForRender`; viewport,
selection, ray and Collection visibility are not evaluated. Transform flags
are retained as source facts, not interpreted as transforms.

Known Object kinds require the corresponding data block code and SDNA type:
Mesh (`ME`/`Mesh`), legacy curve/surface/text (`CU`/`Curve`), metaball
(`MB`/`MetaBall`), light (`LA`/`Lamp`), camera (`CA`/`Camera`), speaker
(`SK`/`Speaker`), probe (`LP`/`LightProbe`), lattice (`LT`/`Lattice`), armature
(`AR`/`bArmature`), legacy grease pencil (`GD`/`bGPdata`), curves
(`CV`/`Curves`), point cloud (`PT`/`PointCloud`), volume (`VO`/`Volume`) and
grease pencil (`GP`/`GreasePencil`). Empty permits null data or `IM`/`Image`.
These are source-ID shape requirements, not geometry compatibility claims.
Missing required data or mismatched code/type fails with
`BLEND_SCENE_REFERENCE_INVALID`. An unmapped type fails explicitly with
`BLEND_SCENE_OBJECT_TYPE_UNSUPPORTED`; it is not silently treated as an Empty.

The scalar `Collection *instance_collection`, stored as `dup_group` in the
current corpus SDNA, is checked even when collection instancing is inactive.
Every nonnull value must resolve exactly to one local `Collection` with
`GR` or `DATA` code and a valid GR-prefixed ID. Bit 8 of `transflag` requires
a nonnull target. The optional `instanceCollectionBlockIndex` refers to the
caller's block sequence. Each distinct instance target outside already visited
Collections/Object/data IDs consumes one visit; shared targets count once.
Instance targets do not extend depth or membership. Their internal lists and
recursive instance edges are not expanded, evaluated or cycle-checked here.

All failures are fatal with source context and no partial result. Existing
reader, linked-ID, name and budget diagnostics retain their behavior. No
production budgets, local/world transform construction, normalized geometry,
unsupported-kind USD fallback, populated Scene IR or importer integration
are introduced. Fixture-backed scope belongs in the
[capability matrix](../reference/CAPABILITY_MATRIX.md#5-scene-ir).

### 5.3 `usdBlendFileFormat` — the importer

The OpenUSD `SdfFileFormat` bundle, scaffolded from OpenStrata's
`usd-fileformat-cpp` template. It owns `.blend` registration, the read path,
stage metadata, prim layout, schema authoring and the diagnostics that cross
the source → USD boundary (§2.2). USD authoring is a separate translation unit
inside the bundle (`src/usd/`), so the `SdfFileFormat` class stays thin.

### 5.4 `blend_inspect` — the tool

Reports what a `.blend` contains — header, blocks, SDNA, datablock counts,
objects, diagnostics — without USD, so a parser question can be answered
without an importer in the way. It is built before the importer reads
geometry, because binary-format work needs a way to look.

```sh
blend_inspect scene.blend
blend_inspect scene.blend --blocks
blend_inspect scene.blend --dna
blend_inspect scene.blend --objects
```

Each invocation validates the full container, one DNA1 schema and raw ID
records before writing a summary. Detail flags can be combined. `--blocks`
reports decoded payload offsets, lengths, saved addresses, SDNA indices and
element counts; `--dna` reports structure sizes and member declarations,
offsets and sizes. `--objects` reports raw Object ID names, including their
stored two-byte prefix, not scene membership, transforms or geometry.
Counts cover all stored ID records, including unused ones. Non-ASCII and
control bytes in names are escaped as `\xNN`; quotes and backslashes are
escaped too, without interpreting or changing the source name.

Compressed inputs require all four caller-supplied limits:
`--max-input-bytes`, `--max-output-bytes`, `--max-expansion-ratio` and
`--max-window-log`. Values are positive decimal integers; window log is
10 through 30. For uncompressed inputs without options, both byte limits
are the file size; no corpus-derived decompression default is chosen.
The block budget is bounded by decoded byte size and unsigned 32-bit indices.
Full-stream limits retain the
[blend contract's semantics](BLEND_CONTRACT.md#41-full-stream-byte-reading).

The summary and details go to stdout; diagnostics go to stderr. Exit status
is 0 on success, including recoverable unsupported-block diagnostics, 1 on
input/validation failures, and 2 on invalid arguments. Header-only synthetic
fixtures are not full containers and fail inspection. Commands and build
modes are in [the build guide](../guides/building.md#inspection-tool).

### 5.5 `blendHost` — the Blender host backend (Phase 7)

A plain C++ library that implements `IBlendBackend` by running a separately
installed Blender executable as a subprocess and reading back a Scene IR. It is
for bootstrapping, oracle comparison and evaluated geometry; it never replaces
the native backend ([BACKEND_POLICY.md §4](BACKEND_POLICY.md#4-the-blender-host-backend)).

### 5.6 `blendUsd` — deferred

USD authoring starts inside `usdBlendFileFormat` (§5.3). It moves to its own
library only when a second consumer — a CLI converter, an exporter tool — needs
it without the file format.

## 6. The schema admission test

A Blender-specific schema or property is admitted only if it passes:

> **Would a consumer benefit from reading this value from USD even if it never
> sees the original `.blend`?**

and only if no standard schema can represent it. If not, the value stays as
provenance in `customData`, or is not authored. Stage-contract v1 has **no
custom schemas**. A value that is preserved without a schema is a namespaced
`blend:*` attribute
([STAGE_CONTRACT.md §3](STAGE_CONTRACT.md#3-authoring-conventions)).

## 7. Geometry policy

- **Objects** become `UsdGeomXform` prims under `/Asset/geo`, with their data
  as a child (`mesh`), so that object transform and datablock stay separate
  ([STAGE_CONTRACT.md §7](STAGE_CONTRACT.md#7-objects-and-transforms)).
- **Meshes** become `UsdGeomMesh`, in this order of priority: topology,
  positions, normals, UVs, material subsets, color attributes, generic
  attributes ([STAGE_CONTRACT.md §8](STAGE_CONTRACT.md#8-meshes)).
- **Curves** become `UsdGeomBasisCurves` or `UsdGeomNurbsCurves`; **point
  clouds** become `UsdGeomPoints`
  ([STAGE_CONTRACT.md §9](STAGE_CONTRACT.md#9-curves-and-points)).
- **Modifiers and Geometry Nodes** are not applied by the native reader
  (§2.3). An object whose visible result depends on them is authored from its
  source mesh with a diagnostic.

## 8. Animation and skinning policy

Not every Blender animation system is reproduced. The order is:

| Step | Scope | Phase |
| --- | --- | --- |
| A | object transform animation (Actions, F-Curves) | 5 |
| B | armature and bone animation → `UsdSkel` | 6 |
| C | shape keys | investigated in 6 |
| D | constraints | later |
| E | drivers, modifiers, Geometry Nodes | host backend only |

Reading saved F-Curve keys and sampling them is a data conversion. Evaluating
constraints and drivers is Blender's dependency graph (§2.3).

## 9. Asset resolution boundary

The file format does not transport assets. HTTP, authentication, signed URLs,
caching and range requests belong to an `ArResolver` such as
`usd-http-resolver`. This repository starts from the resolved asset.

So that it can read through `ArAsset` rather than a filesystem path, the reader
reads through `ByteSource`
([BLEND_CONTRACT.md §3](BLEND_CONTRACT.md#3-byte-sources)), never a hard-wired
`std::ifstream`. Remote `.blend` streaming is not a first-release requirement:
compressed files limit what range access can achieve.

Packed resources (images stored inside the `.blend`) are first reported, not
served. A virtual identity such as `blend://path/to/file.blend#image/<id>` is a
resolver or package-resolver concern and is not taken on by the file format
early ([MATERIAL_POLICY.md §6](MATERIAL_POLICY.md#6-images-and-paths)).

## 10. Parser strategy

A `.blend` is external input. The parser is owned by the project and
defensive:

- every read is bounded by the source size; every size computation is
  overflow-checked;
- block sizes, counts and SDNA indices are validated before use;
- allocation is bounded by what the remaining bytes can hold, and by explicit
  limits where they cannot (decompressed size);
- recursion depth is bounded (ID graph, collections, node trees);
- old pointers are resolved only through the pointer map, never dereferenced;
- malformed SDNA is detected, never trusted;
- decompression is guarded against decompression bombs;
- no exceptions cross a component boundary; failures are diagnostics.

The detail is [BLEND_CONTRACT.md §2](BLEND_CONTRACT.md#2-reading-rules). The
fuzzing and sanitizer lanes (libFuzzer, ASan, UBSan, later OSS-Fuzz) are added
in Phase 8. A third-party `.blend` parser may be adopted only after the checks
in [DEPENDENCIES.md §4](../architecture/DEPENDENCIES.md#4-third-party-code).

## 11. Performance and caching

`Read(..., metadataOnly)` is used from the start. When `metadataOnly` is true
the reader decodes stage metadata, the object list, the hierarchy and asset
information, and does not decode mesh buffers, read textures or expand
animation samples
([STAGE_CONTRACT.md §16](STAGE_CONTRACT.md#16-metadata-only-layers)). `.blend`
files grow large, so this fast path is designed in, even though it is tuned in
Phase 8.

No persistent cache is implemented in the file format early. The cache key is
defined now so that one can be added without changing the architecture:
resolved asset identity, file digest or modification time, reader version and
conversion options. A cached generated `.usdc` is a later option.

## 12. Diagnostics

Every diagnostic has a stable code (`BLEND_<FAMILY>_<EVENT>`), a severity, a
message, a source location (byte offset, block index or datablock name) where
one exists, and a recoverable flag. Severities distinguish **fatal**,
**warning** and **unsupported-but-readable**: the reader authors what it can
and does not break the whole stage for one unsupported datablock. Tests assert
codes, never prose. The record and catalog are in
[reference/DIAGNOSTICS.md](../reference/DIAGNOSTICS.md).

## 13. Testing policy

A format plugin is only as trustworthy as its fixtures.

| Level | Proves | Examples |
| --- | --- | --- |
| 1 — container | header, pointer size, endianness, legacy and Blender 5 block layouts, compression, truncation | valid and invalid headers, truncated files |
| 2 — SDNA | `NAME`, `TYPE`, `TLEN`, `STRC` decoding and member lookup | SDNA from each supported Blender version |
| 3 — Scene IR | decoded objects, meshes, materials, cameras, lights against expected IR | small purpose-made `.blend` files |
| 4 — USD authoring | prim paths, schema types, topology, transforms, materials, metadata from IR fixtures | IR built in code, no `.blend` needed |
| 5 — integration | `Usd.Stage.Open("*.blend")`, `usdcat`, the OpenStrata plugin pyramid | the registered plugin |

- **Golden USDA** covers compact, stable contracts only.
- **Semantic tests** assert meaning over text: paths, types, counts, bindings.
- **Oracle tests** compare the native backend's Scene IR with one produced by
  Blender itself ([BACKEND_POLICY.md §6](BACKEND_POLICY.md#6-blender-as-a-test-oracle));
  they run where Blender is installed and are never a runtime dependency.
- **Fuzzing** (Phase 8) targets `blendFile`'s entry point.

**Fixtures are the project's own.** Byte-level fixtures (headers, blocks,
corruption) are generated by committed scripts. Scene fixtures are written by
committed Blender Python scripts run in a pinned Blender version; their output
is committed so the suite runs without Blender. Fixtures are kept small, and
grouped by the Blender version that wrote them (`blender_3x/`, `blender_4x/`,
`blender_45_lts/`, `blender_5x/`). A `.blend` downloaded from elsewhere is
never committed.

**Corpus files are separate from generated fixtures.** A file supplied directly
by a contributor may be committed only with explicit permission, under
`tests/corpus/` in the owning component. Each version directory carries a
`manifest.json` with the reported Blender version and its evidence, file paths,
byte sizes, SHA-256 hashes, provenance, permission, and reader status. Keep
original bytes; do not imply that corpus files can be regenerated or that their
presence proves reader support. A header version alone does not establish the
Blender patch version or LTS designation.

## 14. Phases

This repository has **one** phase sequence, written `Phase 0`–`Phase 8`, and
this section is its source of truth. A phase is not a release; which release
carries a phase is decided in the
[roadmap](../roadmap/README.md#status-at-a-glance), never here.

| Phase | Goal | Acceptance |
| --- | --- | --- |
| **0 — workspace skeleton** | Root CMake, `VERSION`, OpenStrata configuration, CI on Windows and Linux, the `usdBlendFileFormat` bundle from `usd-fileformat-cpp`, `.blend` registration, a `blendFile` scaffold that recognizes the header, the diagnostic record, a minimal `/Asset` stage. | `Usd.Stage.Open("empty.blend")` returns a stage whose default prim is `/Asset` with `geo` and `mtl`; non-`.blend` bytes fail with a diagnostic; `ost plugin test` passes L0–L5. |
| **1 — container and SDNA** | `ByteSource`, compression, legacy and Blender 5 containers, block headers, DNA1 and SDNA, raw datablock enumeration, `blend_inspect`. | `blend_inspect` reports version, blocks, SDNA and datablock counts for fixtures from every supported Blender version; malformed fixtures produce diagnostics and never crash; `blendFile` links no OpenUSD. |
| **2 — objects and meshes** | `blendScene`; objects, parenting, transforms, meshes, topology, normals, UVs, identifiers; `/Asset/geo`. | `single_cube.blend` opens in `usdview` as `UsdGeomMesh` at `/Asset/geo/Cube/mesh`; hierarchy and transforms match the oracle; the same file always authors the same stage. |
| **3 — materials and images** | `/Asset/mtl`, material slots, `GeomSubset`, the Principled BSDF subset as `UsdPreviewSurface`, external image textures, alpha, normal maps. | Base color, textures, alpha and normal maps visible in generic renderers; every binding targets `/Asset/mtl`. |
| **4 — cameras, lights and collections** | `UsdGeomCamera`, `UsdLux`, collection metadata, improved hierarchy. | Camera framing and light placement match fixtures; conversions for lens and intensity are fixed by fixtures. |
| **5 — object animation** | Actions, F-Curves on object transforms, frame rate and time metadata. | Transform animation plays in `usdview` with Blender's timing. |
| **6 — armatures and UsdSkel** | Bones, skeleton hierarchy, skin weights, bindings, skeletal animation; shape-key investigation. | A skinned fixture deforms in `usdview` as it does in Blender. |
| **7 — Blender host backend** | Optional Blender discovery, `blendHost`, oracle comparison tests, an evaluated-mesh path, modifier and Geometry Nodes experiments. | Oracle tests run in CI where Blender is installed; the native backend stays the default. |
| **8 — performance and robustness** | The `metadataOnly` fast path, lazy decode, fewer allocations, safe parallel decode, ASan/UBSan, fuzzing, a large-scene benchmark, a cache investigation. | Fuzz and sanitizer lanes are green; the benchmark is recorded in a report. |

The first technical goal sits in Phase 2: **a `.blend` containing a cube opens
directly in `usdview` as a `UsdGeomMesh`.**

### 14.1 First stable release

The first release that claims a stable reader contract is done when:

- the native read path is stable, and the supported Blender range — at least
  4.5 LTS and 5.x — is written down
  ([BLEND_CONTRACT.md §9](BLEND_CONTRACT.md#9-version-support));
- USD output is deterministic;
- static geometry, materials, cameras, lights, animation and a `UsdSkel`
  baseline are supported, each with fixtures;
- diagnostics are robust and cataloged;
- the OpenStrata package is reproducible;
- normal use needs no Blender installation.

That is Phases 0–6 and 8; the [roadmap](../roadmap/README.md) decides the
version.

## 15. Decisions frozen early

These are expensive to change once consumers exist, so they are fixed now and
fixture-tested from the first meaningful release. Changing one requires a
stage-contract version bump
([STAGE_CONTRACT.md §2](STAGE_CONTRACT.md#2-contract-version)) or, for the
structural ones, a change to WORKSPACE.md first.

| # | Decision | Fixed in |
| --- | --- | --- |
| 1 | `/Asset` is the root and the default prim | [STAGE_CONTRACT.md §4](STAGE_CONTRACT.md#4-prim-hierarchy) |
| 2 | The `geo` · `mtl` · `skel` · `cameras` · `lights` · `collections` scope vocabulary, independent of Blender scene and collection names | [STAGE_CONTRACT.md §4](STAGE_CONTRACT.md#4-prim-hierarchy) |
| 3 | Y-up, meters, and one conversion layer | [STAGE_CONTRACT.md §6](STAGE_CONTRACT.md#6-coordinate-conversion) |
| 4 | Object transform and data are separate prims (`<Object>/mesh`) | [STAGE_CONTRACT.md §7](STAGE_CONTRACT.md#7-objects-and-transforms) |
| 5 | Source name and USD identifier are separate; identifiers are deterministic | [NAMING_POLICY.md §3](NAMING_POLICY.md#3-identity-versus-display) |
| 6 | The stage-contract version mechanism | [STAGE_CONTRACT.md §2](STAGE_CONTRACT.md#2-contract-version) |
| 7 | `blendFile` → `blendScene` → `usdBlendFileFormat`, with no OpenUSD in the libraries | [WORKSPACE.md §2](../architecture/WORKSPACE.md#2-dependency-directions) |
| 8 | Blender is never linked; it is reached only across a process boundary | [BACKEND_POLICY.md §7](BACKEND_POLICY.md#7-license-boundary) |
| 9 | Read-only: no `.blend` writer | §2.4 |
| 10 | The default path authors source data, not evaluated data | §2.3 |

## 16. Decisions deliberately left flexible

Not frozen until at least two plausible consumers or one real implementation
demonstrate the need: any Blender-specific schema; the MaterialX path; the host
backend's interchange format; whether evaluation mode is exposed as file-format
arguments (`?evaluation=source`, `?evaluation=blender`); the persistent cache;
a packed-resource resolver; Python bindings; `blendUsd` as its own library.

## 17. Non-goals for the first releases

Not pursued in Phases 0–2, and not promised by any later Phase unless it says
so: a USD → `.blend` writer; Blender UI integration; a Blender add-on;
re-implementing the Geometry Nodes evaluator; full Cycles shader parity; EEVEE
renderer semantics; reproducing simulation caches; full driver evaluation;
Python expressions; interactive editing; a packed-resource resolver; remote
`.blend` streaming.

In particular: **the file format is not a Blender runtime replacement.**

## 18. Place in the ecosystem

```text
                 OpenUSD hosts (usdview, usdcat, runtimes)
                                  │
                                  ▼
                         usdBlendFileFormat
                                  │
                    ┌─────────────┴──────────────┐
                    ▼                            ▼
            native backend               blendHost (Phase 7)
         blendScene ← blendFile          Blender process, separately installed
                    │                            │
                    └─────────────┬──────────────┘
                                  ▼
                              Scene IR
                                  │
                                  ▼
                            USD authoring
      UsdGeom · UsdShade · UsdLux · UsdSkel · UsdCollectionAPI
                                  │
                                  ▼
                               SdfLayer
```

| Repository | Owns |
| --- | --- |
| `usd-blender-plugins` | `.blend` reading, Blender-to-USD semantics, the Blender host boundary |
| `usd-mmd-plugins`, `usd-vrm-plugins` | their formats; the same `/Asset` contract |
| `usd-http-resolver` | asset transport: HTTP, authentication, caching, range requests |
| `open-strata` | plugin scaffolding, build, packaging, runtime composition |

## 19. Where this document departs from the implementation plan

The 2026-10-01 implementation plan (v2) is the origin of this document. Where
this document had to be more precise, or align with the sibling repositories,
it departs in the following places. Each departure is `proposed` until the
Phase that first realizes it lands with a fixture, and binding from then.

| Implementation plan | Here | Why | Status |
| --- | --- | --- | --- |
| §3, §32 — `lib/blend/`, `lib/usd/`, `src/fileformat/`; targets `blend_reader`, `blend_usd`, `BlenderFileFormat`; tool `blend-inspect`; plugin `usd-blender` | identities `blendFile`, `blendScene`, `usdBlendFileFormat`, `blend_inspect` in `libs/`, `plugins/`, `tools/` | the sibling workspace naming: lower-camel identities equal to their directory, `snake_case` executables ([WORKSPACE.md §1](../architecture/WORKSPACE.md#1-identities)) | binding workspace identities ([WORKSPACE.md §1](../architecture/WORKSPACE.md#1-identities)) |
| §4.2 — one `blend_reader` holding container, SDNA and Scene IR | `blendFile` (syntax) and `blendScene` (Scene IR, decoding, conversion) | `blend_inspect --blocks/--dna` needs syntax only; the host backend produces a Scene IR with no container reader; each half is testable alone | proposed (Phase 2) |
| §32 — a `blend_usd` static library | USD authoring inside the bundle, in `src/usd/`; `blendUsd` reserved | no second consumer of authoring exists yet; separate translation units keep the `SdfFileFormat` thin (§5.6) | proposed (Phase 2) |
| §37 — releases v0.1.0–v1.0.0 as the plan's units | Phases 0–8 (§14); the release for each Phase only in the roadmap table | one source of truth per fact ([contributing/documentation.md](../contributing/documentation.md#one-source-of-truth-per-fact)) | accepted |
| §9 — top-level metadata only | `/Asset.customData.blend:stageContractVersion` added | layer metadata is lost once the asset is referenced; `/Asset` customData travels with it, as in the siblings ([STAGE_CONTRACT.md §2](STAGE_CONTRACT.md#2-contract-version)) | binding contract version ([STAGE_CONTRACT.md §2](STAGE_CONTRACT.md#2-contract-version)) |

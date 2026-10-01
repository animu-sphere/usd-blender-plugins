# Inspecting a .blend file

Use `blend_inspect` to examine the saved binary structure without launching
Blender or loading OpenUSD. It reports the header, block framing, SDNA schema
and raw ID datablocks, not evaluated scene values or a converted USD stage.
See the [capability matrix](../reference/CAPABILITY_MATRIX.md) for supported
behavior and the [CLI contract](../design/DESIGN_POLICY.md#54-blend_inspect--the-tool)
for the output boundary.

## Build and select the tool

Run these PowerShell commands from the repository root, with the prerequisites
in [Building and testing](building.md#inspection-tool):

```powershell
ost build --target cy2026 --profile usd
ost test --target cy2026 --profile usd
$inspect = './tools/blendInspect/bin/blend_inspect.exe'
& $inspect --help
```

The examples on this page were exercised on Windows on 2026-10-02 with
`ost` 0.23.14 and Visual Studio 2026 (MSVC 19.51). They do not establish Linux
execution evidence. The executable name on Linux has no `.exe` suffix.
Although the workspace build also builds the plugin, the inspection executable
links only the reader; the build guide also describes the reader-only preset.

## Read a summary

```powershell
& $inspect './plugins/usdBlendFileFormat/tests/corpus/blender-4.5.13/Untitled.blend'
```

The summary includes the version encoded in the header, container layout,
pointer width, byte order, decoded byte count, block count, SDNA table counts
and raw ID counts by type. This fixture reports version `4.5 (405)`, a legacy
64-bit little-endian container, 504,759 decoded bytes and 1,240 blocks, including
the terminal `ENDB`. The header does not establish the patch version: `4.5.13`
comes from the [corpus provenance](../../plugins/usdBlendFileFormat/tests/corpus/README.md).

Inspection requires a complete container with exactly one `DNA1` block.
The repository's synthetic header-only fixtures are not complete inspection
inputs. Even a summary reads and validates the full stream, block framing,
schema and raw IDs; detail flags only add output.

## Inspect blocks and SDNA

```powershell
& $inspect './plugins/usdBlendFileFormat/tests/fixtures/empty.blend' --blocks --dna --objects
```

The generated empty scene has 188,558 decoded bytes, 266 blocks and one Scene
ID, with no Object or Mesh IDs. The detail modes can be combined:

- `--blocks` lists the zero-based block index, code, payload offset, payload
  length, stored old address, SDNA index and element count. Offsets refer to
  decoded bytes, not positions in the compressed file. The old address is a
  stored pointer key, not a live process address.
- `--dna` lists structure indices, type names and sizes, followed by each
  member's stored declaration, offset and size. A block's SDNA index selects
  the structure table, not the type table. These are storage layouts, not
  decoded member values.
- `--objects` lists raw Object IDs. An empty section for this fixture is
  expected; it does not indicate an inspection failure.

## Inspect saved Object names

```powershell
& $inspect './plugins/usdBlendFileFormat/tests/corpus/blender-4.5.13/Untitled.blend' --objects
```

The corpus contains three Object IDs and one Mesh ID. Object names retain
their stored `OB` prefixes; quotes and backslashes are escaped, and non-ASCII
or control bytes are printed as `\xNN` byte escapes. Names are not normalized
USD identifiers. The listing does not establish scene membership, transforms,
mesh contents or evaluated geometry.

## Inspect compressed input

The Blender 5.2.2 corpus is Zstandard-compressed:

```powershell
& $inspect './plugins/usdBlendFileFormat/tests/corpus/blender-5.2.2/Untitled.blend' --objects --max-input-bytes 67108864 --max-output-bytes 67108864 --max-expansion-ratio 2048 --max-window-log 23
```

The same four options are required for gzip input. These values are explicit
test budgets, not recommended production defaults; the owning decision is
[BLEND-O5](../design/BLEND_CONTRACT.md#12-open-questions).

| Option | Meaning |
| --- | --- |
| `--max-input-bytes` | Maximum encoded input size in bytes. |
| `--max-output-bytes` | Maximum decoded output size in bytes. |
| `--max-expansion-ratio` | Maximum decoded-to-encoded byte ratio, as a positive integer. |
| `--max-window-log` | Maximum Zstandard window log, from 10 through 30; 23 permits an 8 MiB window. |

All values must be positive decimal integers, and all four options must be
supplied together, without duplicates. The window option is still required
for gzip even though DEFLATE uses its fixed 32 KiB window. Without options,
uncompressed input uses its actual file size as the input/output byte budget.
The full decoded file is held in memory, with additional allocations for
blocks, schema and ID records; the output budget is not a total-memory limit.

## Diagnostics and exit codes

Ordinary output goes to stdout; diagnostics go to stderr. Recoverable
`BLEND_BLOCK_UNKNOWN_CODE` messages on the real corpus do not make the command
fail: unknown block codes are retained, not interpreted as scene features.
Diagnostics may include a byte offset and a zero-based block index. Block,
schema and raw-ID offsets are in decoded-file space; schema-relative offsets
are translated to file offsets by the CLI. Compression diagnostics use the
reader's byte context, not block offsets. See the
[diagnostic reference](../reference/DIAGNOSTICS.md) for individual codes.

| Exit code | Meaning |
| --- | --- |
| `0` | Inspection succeeded, possibly with recoverable diagnostics, or help was printed. |
| `1` | Input, limit, decoding or inspection failure. |
| `2` | Invalid command-line arguments. |

These deliberately failing examples show two different errors; check
`$LASTEXITCODE` immediately after each invocation:

```powershell
& $inspect './plugins/usdBlendFileFormat/tests/corpus/blender-5.2.2/Untitled.blend'
$LASTEXITCODE
& $inspect './plugins/usdBlendFileFormat/tests/corpus/blender-5.2.2/Untitled.blend' --max-input-bytes 67108864
$LASTEXITCODE
```

The first reports fatal `BLEND_COMPRESSION_LIMITS` and returns `1`, because
compressed input has no budgets. The second reports `BLEND_INSPECT_USAGE`
and returns `2`, because only one of the four options was supplied. Neither
command modifies the input. Keep quoted paths when inspecting files whose
names contain spaces.
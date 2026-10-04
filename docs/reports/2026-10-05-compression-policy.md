# Compression policy measurements — 2026-10-05

This is dated Windows execution evidence for
[BLEND-O5's accepted policy](../design/BLEND_CONTRACT.md#42-standard-full-stream-limit-policy),
not a current capability or release-status page.

## Environment and input provenance

- Windows, MSVC 19.51, CMake's `reader` preset, Release.
- Blender 4.5.13 LTS, build `daeeeca98fb0`, bundled NumPy 1.26.4.
- Blender 5.2.2 LTS, build `d13f752e3b9c`, bundled NumPy 2.3.4.
- Inputs generated locally by
  [generate_compression.py](../../tests/fixtures/generate_compression.py),
  `policy` profile, NumPy seed 20261005. No downloaded assets, external
  textures, user scenes or embedded scripts are used.

Each version generated these three cases:

| Case | Saved source data |
| --- | --- |
| `large_mesh` | 524,288 independent triangles, 1,572,864 vertices, seeded non-repetitive float positions and generated edges |
| `repetitive` | 2,097,152 coincident points and eight dense, zero-valued point-domain float attributes |
| `packed_assets` | one triangle and a material referencing four packed 2048x2048 seeded-noise RGBA images |

Each case was written with Blender's `bpy.data.libraries.write`, both
uncompressed and with `compress=True`. The compressed outputs have Zstandard
magic. Python's standard-library gzip encoder separately wrapped each
uncompressed file at level 9, with an empty filename and zero timestamp.
**These gzip streams are generated encodings of Blender-written containers,
not files saved with an old Blender gzip writer.**

The generator reopened both Blender-written outputs with scripts disabled
and checked saved mesh counts, zero attributes or packed-image dimensions
and presence. These are library containers, not importer-ready active-Scene
fixtures, and establish no native Scene-decoder or material support.

## Measurements

`blendFileTests --measure-compression FILE...` uses the existing
`ReadFileBytes` implementation. Its exploration ceilings are 2 GiB input,
2 GiB output, ratio 65,536 and window log 30; these are measurement budgets,
not accepted defaults. It then checks:

- exact stored and decoded byte budgets preserve every decoded byte;
- one-byte-smaller input/output limits produce
  `BLEND_COMPRESSION_INPUT_LIMIT` / `BLEND_COMPRESSION_OUTPUT_LIMIT`;
- one-integer-smaller expansion ratios produce
  `BLEND_COMPRESSION_RATIO_LIMIT`;
- every window log below the first successful one produces
  `BLEND_COMPRESSION_WINDOW_LIMIT`.

The integer ratio is `ceil(decoded / stored)`. The window column is the
smallest accepted API value, not a parsed frame-header measurement. For
gzip it is 10 because that field does not restrict DEFLATE's fixed 32 KiB
window.

| Blender | Case | Encoding | Stored bytes | Decoded bytes | Minimum integer ratio | Minimum accepted window log |
| --- | --- | --- | --- | --- | --- | --- |
| 4.5.13 | large_mesh | Blender Zstandard | 32,945,328 | 49,984,057 | 2 | 20 |
| 4.5.13 | repetitive | Blender Zstandard | 70,379 | 94,548,869 | 1,344 | 20 |
| 4.5.13 | packed_assets | Blender Zstandard | 67,308,065 | 67,461,474 | 2 | 20 |
| 4.5.13 | large_mesh | generated gzip | 26,454,222 | 49,984,057 | 2 | 10 |
| 4.5.13 | repetitive | generated gzip | 154,064 | 94,548,869 | 614 | 10 |
| 4.5.13 | packed_assets | generated gzip | 67,323,107 | 67,461,474 | 2 | 10 |
| 5.2.2 | large_mesh | Blender Zstandard | 32,708,270 | 48,427,721 | 2 | 20 |
| 5.2.2 | repetitive | Blender Zstandard | 71,511 | 94,565,524 | 1,323 | 20 |
| 5.2.2 | packed_assets | Blender Zstandard | 67,308,407 | 67,477,807 | 2 | 20 |
| 5.2.2 | large_mesh | generated gzip | 26,448,167 | 48,427,721 | 2 | 10 |
| 5.2.2 | repetitive | generated gzip | 155,453 | 94,565,524 | 609 | 10 |
| 5.2.2 | packed_assets | generated gzip | 67,323,617 | 67,477,807 | 2 | 10 |

The accepted policy was separately supplied explicitly to `blend_inspect`:
input 268,435,456 bytes, output 536,870,912 bytes, ratio 4,096, window log 23.
All 18 containers (six uncompressed, six Blender Zstandard, six generated
gzip) completed full-stream, block, SDNA, pointer-map and raw-ID inspection.
The packed cases each reported four Image IDs; every case reported one
Mesh, one Object and one Scene.

The largest stored compressed input is about 64.20 MiB; largest decoded
output is about 90.18 MiB. The repetitive case shows that a 1,024x ratio
limit would reject legitimate Blender-written dense source data even at
this bounded scale. The policy's headroom and limitations belong to the
[owning contract](../design/BLEND_CONTRACT.md#42-standard-full-stream-limit-policy).

## Run identity

The generator records sizes, SHA-256, Blender build, NumPy version, profile,
seed and per-codec measurements in a local `manifest.json`. The SHA-256
values below preserve the identity of this run without committing the large
binary outputs. Library writes are not asserted byte-identical across saves
or processes; regenerated sizes and hashes may differ. Exact-limit checks
compare each stream's own decoded bytes, not the bytes of a separate save.

| Blender | File | SHA-256 |
| --- | --- | --- |
| 4.5.13 | large_mesh.raw.blend | `dd3d93a5e9088e442ee201b498f56eebd7c8c130f1354b271901ad233f2bab2f` |
| 4.5.13 | repetitive.raw.blend | `8229a9b30a49268194ab117343ebb708964f8646cacecb8e008cc25b32d2d499` |
| 4.5.13 | packed_assets.raw.blend | `2164b9af2095ca7416933224ee37722ac95ca720cbea8776587cbfbff808303d` |
| 4.5.13 | large_mesh.blend | `c27ed4081248fa6ad23ee7b63b06833145bd1ad73b4053d33b982593a9b087e3` |
| 4.5.13 | repetitive.blend | `759f78578f086192bf14c58dce93e1fd3be7f8e61631a97f4d8c32a6565717cd` |
| 4.5.13 | packed_assets.blend | `25007b21440ce73fcb42ee90ef625850edaefd244577273c579f8960735a8c63` |
| 4.5.13 | large_mesh.gzip.blend | `dfa76d911ed13237114ce56ea21c1dd29e1954774017c924b36ccc7b51cea8df` |
| 4.5.13 | repetitive.gzip.blend | `bb6f5e19cd2a2b0fcb449779ee36e9032a6a3329325c3780337dfd7117140b71` |
| 4.5.13 | packed_assets.gzip.blend | `d534b2dc41f0cbbc776bdb813abe627efe895ac36f857c3c11e44580c4e59ea4` |
| 5.2.2 | large_mesh.raw.blend | `86f64fcf6014228d5d0afa4a75155ad8a168166b2304951aece43a06d27d1f73` |
| 5.2.2 | repetitive.raw.blend | `535aa6c14df17becd29fa5c923e7aa18d69fcfd0fb8d6e31591f0739af49955b` |
| 5.2.2 | packed_assets.raw.blend | `293dce9e5959b856fdf3d42c175e3f286086e7c65303ea13759e7e46d13c7ae4` |
| 5.2.2 | large_mesh.blend | `84ddb34a36c65c94dfe548ab203c5c66bdabd755339cb9cd8aa7bd5cafc54a58` |
| 5.2.2 | repetitive.blend | `026022782d52497ff2b707d4aebea5ea6ddf06895abd07d939e5d92febb33061` |
| 5.2.2 | packed_assets.blend | `7befb9d0100771fee6986217eba641ea9f219da15979bee7470c959d5829f689` |
| 5.2.2 | large_mesh.gzip.blend | `61c3602bdcdb505953a41c7c1349da0541fc90ce4114cf559770799c1af04563` |
| 5.2.2 | repetitive.gzip.blend | `911e2928bd519b597a5d14baedf1815d5384f30a1a454207951fded998dac5db` |
| 5.2.2 | packed_assets.gzip.blend | `5742abb60c57ac5f61615e6ac4a8aca81dfe0d26975e542c9e5d836cadd8303e` |

## Reproduction and regression checks

From the repository root, with `$blender` set to one of the pinned Blender
executables and a fresh output directory:

```powershell
cmake --build --preset reader --target blendFileTests blend_inspect
$reader = (Resolve-Path 'build\reader-preset\libs\blendFile\Release\blendFileTests.exe').Path
& $blender --background --factory-startup --disable-autoexec --python-exit-code 1 --python tests\fixtures\generate_compression.py -- --output build\compression-policy\fresh-run --reader $reader --profile policy
& $blender --background --factory-startup --disable-autoexec --python-exit-code 1 --python tests\fixtures\test_generate_compression.py -- --reader $reader
ctest --preset reader -R '^(blendFile\.header|blendInspect\.cli)$'
```

The generator requires a reader executable and refuses to overwrite named
outputs or an existing manifest. `--profile small` exercises the same paths
with bounded tiny inputs; `--case large_mesh`, `--case repetitive` and
`--case packed_assets` can select cases. The generator regressions check
saved-file hashes, evidence shape and ratio arithmetic, independent gzip
byte identity, overwrite refusal and explicit missing-reader failure.

To repeat the accepted-policy inspection on each generated file:

```powershell
tools\blendInspect\bin\blend_inspect.exe build\compression-policy\fresh-run\repetitive.blend --max-input-bytes 268435456 --max-output-bytes 536870912 --max-expansion-ratio 4096 --max-window-log 23
```

Both Blender versions exercised the small and policy profiles locally.
This report provides no Linux execution evidence, no GiB-scale scene
certification, and no measured peak-memory or concurrency guarantee.

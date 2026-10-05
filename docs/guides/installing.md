# Installing release packages

Use a matching OpenUSD/Python environment from the binary configurations in
[the release record](../releases/v0.1.0.md#binary-configurations). A plugin
compiled against a different OpenUSD build or ABI is not interchangeable.

## Download and verify

Download `SHA256SUMS` and the archive for your target from
[GitHub Releases](https://github.com/animu-sphere/usd-blender-plugins/releases).
Prefer the aggregate `usd-blender-plugins-<version>-<target>-plugin-product.tar.zst`
when you want both the plugin and `blend_inspect`. The separate plugin and
tool archives are independently installable.

Compare the downloaded file's SHA-256 with its line in `SHA256SUMS`. PowerShell
provides `Get-FileHash -Algorithm SHA256`; Linux provides `sha256sum`.
Do not use a package whose hash differs.

Extract the `.tar.zst` using a Zstandard-capable archive tool into an empty
directory outside your source checkout. The aggregate has
`bundles/usdBlendFileFormat/` and `tools/blend_inspect/`; a separate plugin
archive has the bundle contents at its extraction root.

## Activate

First activate your matching OpenUSD runtime. For an OpenStrata-managed runtime:

```powershell
ost env cy2026 --profile usd --shell powershell
```

Evaluate the emitted environment commands in the current shell. Then
dot-source the plugin's generated `activate.ps1` in PowerShell, or source
`activate.sh` in Bash. For the aggregate, these files are under
`bundles/usdBlendFileFormat/`.

The scripts add plugin registration and packaged native-library paths; they
do not install OpenUSD. For a Python host, add the bundle root to `sys.path`
and import its generated `openstrata_activate` module **before** importing
`pxr`. This retains the Windows DLL-directory handles required by Python 3.8+.

The plugin can then open a supported `.blend` using `Usd.Stage.Open`, or be
used as a USD reference. Its source-data limits and unsupported features are
in [the capability matrix](../reference/CAPABILITY_MATRIX.md).

The aggregate's CLI is `tools/blend_inspect/bin/blend_inspect` (`.exe` on
Windows). It does not need OpenUSD; see [the inspection guide](inspecting.md).

For a runnable installation check against an existing package report, use
[Test-ReleaseProduct.ps1](../../scripts/Test-ReleaseProduct.ps1). It activates
the local pinned runtime, extracts the product to a temporary directory, runs
the registered stage tests and CLI there, and checks that plugin discovery
does not leak from the source checkout.

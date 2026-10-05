param(
    [Parameter(Mandatory)][string]$PackageReport,
    [string]$Platform = 'cy2026',
    [string]$Profile = 'usd'
)

$ErrorActionPreference = 'Stop'
$report = Get-Content -Raw -LiteralPath $PackageReport | ConvertFrom-Json
if (-not $report.ok -or -not $report.data.product.archive) {
    throw 'Package report does not contain a successful aggregate product.'
}
$repository = Split-Path -Parent $PSScriptRoot
$temporary = Join-Path ([IO.Path]::GetTempPath()) ("usd-blender-release-" + [guid]::NewGuid())
$product = Join-Path $temporary 'product'
$smoke = Join-Path $temporary 'smoke'
$testDirectory = Join-Path $smoke 'plugins/usdBlendFileFormat/tests'
$environment = @{}
foreach ($name in @('PATH', 'PYTHONPATH', 'PXR_PLUGINPATH_NAME', 'LD_LIBRARY_PATH', 'DYLD_LIBRARY_PATH', 'CMAKE_PREFIX_PATH')) {
    $environment[$name] = [Environment]::GetEnvironmentVariable($name, 'Process')
}

try {
    New-Item -ItemType Directory -Path $product, $testDirectory, (Join-Path $smoke 'tests') | Out-Null
    tar -xf $report.data.product.archive -C $product
    if ($LASTEXITCODE -ne 0) { throw 'Product extraction failed.' }
    $bundle = Join-Path $product 'bundles/usdBlendFileFormat'
    Copy-Item -LiteralPath (Join-Path $repository 'plugins/usdBlendFileFormat/tests/test_stage.py') -Destination $testDirectory
    Copy-Item -LiteralPath (Join-Path $bundle 'tests/fixtures') -Destination $testDirectory -Recurse
    Copy-Item -LiteralPath (Join-Path $repository 'plugins/usdBlendFileFormat/tests/corpus') -Destination $testDirectory -Recurse
    Copy-Item -LiteralPath (Join-Path $repository 'tests/fixtures') -Destination (Join-Path $smoke 'tests') -Recurse

    $env:PYTHONPATH = ''
    $env:PXR_PLUGINPATH_NAME = ''
    $activation = ost env $Platform --profile $Profile --shell powershell
    if ($LASTEXITCODE -ne 0) { throw 'Runtime activation failed.' }
    Invoke-Expression ($activation -join "`n")
    $runtimePluginPath = $env:PXR_PLUGINPATH_NAME
    $runtimePythonPath = $env:PYTHONPATH
    . (Join-Path $bundle 'activate.ps1')
    $env:PYTHONPATH = $bundle + [IO.Path]::PathSeparator + $env:PYTHONPATH
    $code = @'
import sys
from pathlib import Path
import openstrata_activate
from pxr import Plug, Sdf
import runpy

bundle, script = map(Path, sys.argv[1:])
assert Sdf.FileFormat.FindByExtension("blend") is not None
plugin = Plug.Registry().GetPluginWithName("UsdBlendFileFormatFileFormat")
assert plugin is not None
assert Path(plugin.path).resolve().is_relative_to(bundle.resolve()), plugin.path
sys.argv = [str(script)]
runpy.run_path(str(script), run_name="__main__")
'@
    Push-Location $temporary
    try {
        python -c $code $bundle (Join-Path $testDirectory 'test_stage.py')
        if ($LASTEXITCODE -ne 0) { throw 'Product-only stage-contract tests failed.' }
        $executable = if ($IsWindows) { 'blend_inspect.exe' } else { 'blend_inspect' }
        & (Join-Path $product "tools/blend_inspect/bin/$executable") (Join-Path $bundle 'tests/fixtures/single_cube.blend') --objects
        if ($LASTEXITCODE -ne 0) { throw 'Product-only inspection CLI failed.' }
        $env:PXR_PLUGINPATH_NAME = $runtimePluginPath
        $env:PYTHONPATH = $runtimePythonPath
        python -c 'from pxr import Sdf; assert Sdf.FileFormat.FindByExtension("blend") is None, "Source-tree plugin discovery leaked into the product test"'
        if ($LASTEXITCODE -ne 0) { throw 'Product-free discovery did not fail closed.' }
    }
    finally {
        Pop-Location
    }
}
finally {
    foreach ($name in $environment.Keys) {
        [Environment]::SetEnvironmentVariable($name, $environment[$name], 'Process')
    }
    if (Test-Path -LiteralPath $temporary) {
        Remove-Item -LiteralPath $temporary -Recurse -Force
    }
}

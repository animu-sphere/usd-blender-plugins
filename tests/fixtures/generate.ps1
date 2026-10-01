param([switch]$Check)
$ErrorActionPreference = 'Stop'
$destination = Join-Path $PSScriptRoot '../../plugins/usdBlendFileFormat/tests/fixtures'
$ascii = [System.Text.Encoding]::ASCII
$fixtures = [ordered]@{
    'header_only.blend' = $ascii.GetBytes('BLENDER-v405')
    'invalid.blend' = $ascii.GetBytes('INVALID-v405')
    'truncated.blend' = $ascii.GetBytes('BLENDER-v40')
    'pointer_size.blend' = $ascii.GetBytes('BLENDER?v405')
    'endianness.blend' = $ascii.GetBytes('BLENDER-?405')
    'version.blend' = $ascii.GetBytes('BLENDER-v4x5')
}
foreach ($fixture in $fixtures.GetEnumerator()) {
    $path = Join-Path $destination $fixture.Key
    if ($Check) {
        $actual = [System.IO.File]::ReadAllBytes($path)
        if ([Convert]::ToHexString($actual) -ne [Convert]::ToHexString($fixture.Value)) {
            throw "Fixture differs from generator: $($fixture.Key)"
        }
    } else {
        [System.IO.File]::WriteAllBytes($path, $fixture.Value)
    }
}
Write-Output "Verified $($fixtures.Count) fixtures (Check=$Check)"
param(
    [ValidateSet('x64', 'Win32', 'Both')][string]$Platform = 'Both'
)
$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$entries = [ordered]@{}
$architectures = if ($Platform -eq 'Both') { @('Win32', 'x64') } else { @($Platform) }
foreach ($architecture in $architectures) {
    $dll = Join-Path $root "build/$architecture/Release/foo_input_mdxmini.dll"
    $image = [IO.File]::ReadAllBytes($dll)
    $pe = [BitConverter]::ToInt32($image, 0x3c)
    $expected = if ($architecture -eq 'Win32') { 0x014c } else { 0x8664 }
    if ([BitConverter]::ToUInt32($image, $pe) -ne 0x00004550 -or
        [BitConverter]::ToUInt16($image, $pe + 4) -ne $expected) {
        throw "Expected a $architecture component DLL at $dll."
    }
    $entry = if ($architecture -eq 'Win32') { 'foo_input_mdxmini.dll' } else { 'x64/foo_input_mdxmini.dll' }
    $entries.Add($entry, $dll)
}
$entries.Add('README.md', (Join-Path $root 'README.md'))
$entries.Add('notices/mdxmini-COPYING', (Join-Path $root 'vendor/mdxmini/COPYING'))
$entries.Add('notices/mdxmini-AUTHORS', (Join-Path $root 'vendor/mdxmini/AUTHORS'))
$entries.Add('notices/sdk-license.txt', (Join-Path $root '../SDK-2026-10-01/sdk-license.txt'))
$entries.Add('notices/pfc-license.txt', (Join-Path $root '../SDK-2026-10-01/pfc/pfc-license.txt'))

$name = switch ($Platform) {
    'Win32' { 'foo_input_mdxmini-x86' }
    'Both' { 'foo_input_mdxmini-unified' }
    default { 'foo_input_mdxmini' }
}
$dist = (New-Item -ItemType Directory -Force (Join-Path $root 'dist')).FullName
$package = Join-Path $dist "$name.fb2k-component"
$temporary = Join-Path $dist "$name-$([guid]::NewGuid()).zip"
Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem
try {
    $archive = [IO.Compression.ZipFile]::Open($temporary, [IO.Compression.ZipArchiveMode]::Create)
    try {
        foreach ($entry in $entries.GetEnumerator()) {
            [IO.Compression.ZipFileExtensions]::CreateEntryFromFile(
                $archive, $entry.Value, $entry.Key, [IO.Compression.CompressionLevel]::Optimal) | Out-Null
        }
    } finally { $archive.Dispose() }
    Move-Item -LiteralPath $temporary -Destination $package -Force
} finally {
    if (Test-Path -LiteralPath $temporary) { Remove-Item -LiteralPath $temporary }
}
Write-Host "Packaged $package"

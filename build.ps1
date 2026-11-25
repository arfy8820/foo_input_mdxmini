param(
    [ValidateSet('x64','Win32')][string]$Platform = 'x64',
    [ValidateSet('v143','v145')][string]$PlatformToolset = 'v145',
    [string]$FoobarDirectory,
    [switch]$ReuseSdkBuild,
    [switch]$SkipUiTests
)
$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$installation = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$installation) { throw 'Install Visual Studio C++ Build Tools with x86/x64 support.' }
$msbuild = Join-Path $installation 'MSBuild/Current/Bin/MSBuild.exe'
$options = @('/m','/nr:false','/nologo','/v:minimal','/p:Configuration=Release',"/p:Platform=$Platform",
    "/p:PlatformToolset=$PlatformToolset",'/p:TrackFileAccess=false')
$output = Join-Path $root "build/$Platform/Release"
$oldTemp = $env:TEMP
$oldTmp = $env:TMP
$oldPath = $env:PATH
try {
    $env:TEMP = (New-Item -ItemType Directory -Force (Join-Path $root 'build/temp')).FullName
    $env:TMP = $env:TEMP
    if ($ReuseSdkBuild) {
        & $msbuild (Join-Path $root 'mdxmini_engine.vcxproj') @options
        if ($LASTEXITCODE -ne 0) { throw 'MDX engine build failed.' }
        & $msbuild (Join-Path $root 'foo_input_mdxmini.vcxproj') @options /p:BuildProjectReferences=false
    } else {
        & $msbuild (Join-Path $root 'foo_input_mdxmini.vcxproj') @options
    }
    if ($LASTEXITCODE -ne 0) { throw 'MDX component build failed.' }
    & $msbuild (Join-Path $root 'engine_smoke.vcxproj') @options /p:BuildProjectReferences=false
    if ($LASTEXITCODE -ne 0) { throw 'MDX engine smoke test did not build.' }
    & (Join-Path $output 'engine_smoke.exe') (Join-Path $root "build/fixtures-$Platform")
    if ($LASTEXITCODE -ne 0) { throw 'MDX engine smoke test failed.' }

    if (!$SkipUiTests) {
        if (!$FoobarDirectory) {
            $programFiles = if ($Platform -eq 'Win32' -and ${env:ProgramFiles(x86)}) { ${env:ProgramFiles(x86)} } else { $env:ProgramFiles }
            $FoobarDirectory = Join-Path $programFiles 'foobar2000'
        }
        $shared = Get-Item -LiteralPath (Join-Path $FoobarDirectory 'shared.dll')
        $image = [IO.File]::ReadAllBytes($shared.FullName)
        $expected = if ($Platform -eq 'Win32') { 0x014c } else { 0x8664 }
        if ([BitConverter]::ToUInt16($image, [BitConverter]::ToInt32($image, 0x3c) + 4) -ne $expected) {
            throw "Pass -FoobarDirectory pointing to a $Platform foobar2000 installation."
        }
        $env:PATH = $shared.DirectoryName + ';' + $oldPath
        & $msbuild (Join-Path $root 'ui_smoke.vcxproj') @options /p:BuildProjectReferences=false
        if ($LASTEXITCODE -ne 0) { throw 'MDX UI smoke test did not build.' }
        & (Join-Path $output 'ui_smoke.exe')
        if ($LASTEXITCODE -ne 0) { throw 'MDX UI smoke test failed.' }
        $env:PATH = $oldPath
    } else { Write-Warning 'MDX UI smoke tests explicitly skipped.' }

    $dist = (New-Item -ItemType Directory -Force (Join-Path $root 'dist')).FullName
    $suffix = if ($Platform -eq 'Win32') { '-x86' } else { '-x64' }
    $dllEntry = if ($Platform -eq 'Win32') { 'foo_input_mdxmini.dll' } else { 'x64/foo_input_mdxmini.dll' }
    $zip = Join-Path $dist "foo_input_mdxmini$suffix.zip"
    $package = Join-Path $dist "foo_input_mdxmini$suffix.fb2k-component"
    Add-Type -AssemblyName System.IO.Compression,System.IO.Compression.FileSystem
    $stream = [IO.File]::Open($zip, [IO.FileMode]::Create)
    $archive = [IO.Compression.ZipArchive]::new($stream, [IO.Compression.ZipArchiveMode]::Create)
    try {
        $entries = [ordered]@{
            $dllEntry = Join-Path $output 'foo_input_mdxmini.dll'
            'README.md' = Join-Path $root 'README.md'
            'notices/mdxmini-COPYING' = Join-Path $root 'vendor/mdxmini/COPYING'
            'notices/mdxmini-AUTHORS' = Join-Path $root 'vendor/mdxmini/AUTHORS'
            'notices/foobar2000-sdk-license.txt' = Join-Path $root '../SDK-2026-10-01/sdk-license.txt'
            'notices/pfc-license.txt' = Join-Path $root '../SDK-2026-10-01/pfc/pfc-license.txt'
        }
        foreach ($entry in $entries.GetEnumerator()) {
            [IO.Compression.ZipFileExtensions]::CreateEntryFromFile(
                $archive, $entry.Value, $entry.Key, [IO.Compression.CompressionLevel]::Optimal) | Out-Null
        }
    } finally { $archive.Dispose(); $stream.Dispose() }
    Move-Item -LiteralPath $zip -Destination $package -Force
    Write-Host "Built $package"
} finally {
    $env:TEMP = $oldTemp
    $env:TMP = $oldTmp
    $env:PATH = $oldPath
}

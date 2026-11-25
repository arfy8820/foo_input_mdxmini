param(
    [Parameter(Mandatory)][string]$FoobarDirectory,
    [ValidateSet('x64','Win32')][string]$Platform = 'x64',
    [ValidateSet('v143','v145')][string]$PlatformToolset = 'v145'
)
$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$output = Join-Path $root "build/$Platform/Release"
$fixtures = Join-Path $root "build/fixtures-$Platform"
$hostRoot = Join-Path $root "build/host-$Platform"
$hostFixtures = Join-Path $hostRoot 'fixtures'
$hostComponents = Join-Path $hostRoot 'components'
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$installation = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$installation) { throw 'Install Visual Studio C++ Build Tools.' }
$msbuild = Join-Path $installation 'MSBuild/Current/Bin/MSBuild.exe'
$oldTemp = $env:TEMP
$oldTmp = $env:TMP
$oldTest = $env:MDXMINI_TEST_DIR
try {
    $env:TEMP = (New-Item -ItemType Directory -Force (Join-Path $root 'build/temp')).FullName
    $env:TMP = $env:TEMP
    & $msbuild (Join-Path $root 'host_smoke.vcxproj') /m /nr:false /nologo /v:minimal /p:Configuration=Release "/p:Platform=$Platform" "/p:PlatformToolset=$PlatformToolset" /p:TrackFileAccess=false /p:BuildProjectReferences=false
    if ($LASTEXITCODE -ne 0) { throw 'MDX host test build failed.' }
    $hostExe = Join-Path $hostRoot 'foobar2000.exe'
    New-Item -ItemType Directory -Force $hostComponents,$hostFixtures,(Join-Path $hostFixtures 'no-bank') | Out-Null
    Get-ChildItem -LiteralPath $FoobarDirectory -File | Where-Object {
        $_.Extension -eq '.dll' -or $_.Name -in @('foobar2000.exe','fooHelper.exe')
    } | Copy-Item -Destination $hostRoot -Force
    foreach ($folder in @('runtime','themes','icons','doc')) {
        $source = Join-Path $FoobarDirectory $folder
        if (Test-Path -LiteralPath $source -PathType Container) { Copy-Item -LiteralPath $source -Destination $hostRoot -Recurse -Force }
    }
    foreach ($name in @('foo_ui_std','foo_input_std','foo_converter','foo_dsp_std','foo_dsp_eq','foo_fileops','foo_freedb2','foo_out_upnp','foo_unpack')) {
        $source = Join-Path $FoobarDirectory "components/$name.dll"
        if (Test-Path -LiteralPath $source -PathType Leaf) { Copy-Item -LiteralPath $source -Destination $hostComponents -Force }
    }
    foreach ($dll in @('foo_input_mdxmini.dll','foo_mdxmini_test.dll')) {
        Copy-Item -LiteralPath (Join-Path $output $dll) -Destination $hostComponents -Force
    }
    foreach ($name in @('smoke.mdx','smoke.pdx')) {
        Copy-Item -LiteralPath (Join-Path $fixtures $name) -Destination $hostFixtures -Force
    }
    Copy-Item -LiteralPath (Join-Path $fixtures 'smoke.mdx') -Destination (Join-Path $hostFixtures 'no-bank') -Force
    [IO.File]::WriteAllText((Join-Path $hostRoot 'portable_mode_enabled'), '')
    $result = Join-Path $hostFixtures 'result.txt'
    [IO.File]::WriteAllText($result, '')
    $env:MDXMINI_TEST_DIR = $hostFixtures
    $process = Start-Process -FilePath $hostExe -WorkingDirectory $hostRoot -WindowStyle Hidden -PassThru
    if (!$process.WaitForExit(30000)) { Stop-Process -Id $process.Id }
    $log = [IO.File]::ReadAllText($result)
    Write-Host $log
    if (!$log.StartsWith('PASS:')) { throw "Isolated MDX host test failed. See $result." }
} finally {
    $env:TEMP = $oldTemp
    $env:TMP = $oldTmp
    $env:MDXMINI_TEST_DIR = $oldTest
}

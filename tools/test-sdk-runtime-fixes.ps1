[CmdletBinding()]
param([string]$SdkRoot = 'C:\Program Files (x86)\Steam\steamapps\common\Source SDK Base 2013 Multiplayer')
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vsRoot = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsRoot) { throw 'Visual Studio C++ Build Tools are required.' }
& (Join-Path $vsRoot 'Common7\Tools\Launch-VsDevShell.ps1') -Arch amd64 -HostArch amd64 -SkipAutomaticLocation | Out-Null
$output = Join-Path $repoRoot 'artifacts/pr2171-tests'
New-Item -ItemType Directory -Force -Path $output | Out-Null
$compileLog = Join-Path $output 'build.log'
$executable = Join-Path $output 'sdk-tests.exe'
Push-Location $repoRoot
try {
    $compilerArguments = @(
        '/nologo', '/EHsc', '/MT', '/O2', '/Gy', '/Gw', '/DWIN32', '/DWIN64',
        '/DPLATFORM_64BITS', '/DCOMPILER_MSVC', '/DCOMPILER_MSVC64', '/D_CRT_SECURE_NO_WARNINGS',
        '/Isrc/public', '/Isrc/public/tier0', '/Isrc/public/tier1', '/Isrc/common',
        'tools/test-sdk-runtime-fixes.cpp', 'src/common/imageutils.cpp',
        "/Fo$output/", "/Fe$executable", "/Fd$output/sdk-tests.pdb",
        '/link', '/OPT:REF', '/LIBPATH:src/lib/public/x64', '/LIBPATH:src/lib/common/x64/2015/release',
        'tier1.lib', 'tier2.lib', 'tier3.lib', 'tier0.lib', 'vstdlib.lib',
        'bitmap.lib', 'vtf.lib', 'mathlib.lib', 'jpeglib.lib', 'libspng.lib', 'libz.lib',
        'gdi32.lib', 'user32.lib'
    )
    & cl @compilerArguments *> $compileLog
    if ($LASTEXITCODE -ne 0) { Get-Content -LiteralPath $compileLog; throw 'Regression test build failed.' }
    $env:PATH = (Join-Path $SdkRoot 'bin/x64') + ';' + $env:PATH
    & $executable $output
    if ($LASTEXITCODE -ne 0) { throw 'SDK regression tests failed.' }
} finally { Pop-Location }

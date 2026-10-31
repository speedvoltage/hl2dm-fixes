[CmdletBinding()]
param([int]$Jobs = 4)
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $vswhere)) { throw 'Install Visual Studio 2022 C++ Build Tools first.' }
$msbuild = & $vswhere -latest -products '*' -requires Microsoft.Component.MSBuild -find 'MSBuild\**\Bin\MSBuild.exe' | Select-Object -First 1
if (-not $msbuild) { throw 'MSBuild was not found.' }
$python = Join-Path $env:LOCALAPPDATA 'Programs\Python\Python313'
if (Test-Path -LiteralPath "$python\python.exe") { $env:PATH = "$python;$env:PATH" }
Push-Location (Join-Path $repoRoot 'src')
try {
    & .\devtools\bin\vpc.exe /hl2mp /win64 /define:SOURCESDK +game /mksln hl2mp_matchmaking.sln
    if ($LASTEXITCODE -ne 0) { throw 'VPC generation failed.' }
    & $msbuild hl2mp_matchmaking.sln /t:Build /p:Configuration=Release /p:Platform=win64 "/m:$Jobs" /nologo /verbosity:minimal
    if ($LASTEXITCODE -ne 0) { throw 'Windows build failed.' }
}
finally { Pop-Location }

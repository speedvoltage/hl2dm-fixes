[CmdletBinding()]
param(
    [string]$SdkRoot = 'C:\Program Files (x86)\Steam\steamapps\common\Source SDK Base 2013 Multiplayer',
    [ValidateRange(640, 7680)][int]$Width = 1280,
    [ValidateRange(480, 4320)][int]$Height = 720,
    [switch]$LobbyPreview
)
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$modRoot = Join-Path $repoRoot 'game/mod_hl2mp'
if (Get-Process -Name hl2_win64 -ErrorAction SilentlyContinue) { throw 'Close the running Source client before launching this preview.' }
if (-not (Test-Path -LiteralPath (Join-Path $modRoot 'maps/dm_lockdown.bsp'))) { & (Join-Path $PSScriptRoot 'prepare-content.ps1') }
$identity = [regex]::Match((Get-Content -Raw -LiteralPath (Join-Path $modRoot 'gameinfo.txt')), 'SteamAppId\s+(\d+)').Groups[1].Value
if (-not $identity) { throw 'gameinfo.txt does not contain a Steam AppID.' }
if ($LobbyPreview) {
    $identity = '320'
    Write-Output 'AppID 320 lobby preview: the SDK dedicated server does not accept this identity. Use the default launch for gameplay.'
}
$runtime = Join-Path $repoRoot 'artifacts/client-launch'
New-Item -ItemType Directory -Force -Path $runtime | Out-Null
# Keep the development AppID file outside the installed SDK and release package.
$identity | Set-Content -LiteralPath (Join-Path $runtime 'steam_appid.txt') -Encoding Ascii
$previousApp = $env:SteamAppId
$previousGame = $env:SteamGameId
try {
    $env:SteamAppId = $identity
    $env:SteamGameId = $identity
    $arguments = '-steam -game "' + $modRoot + '" -windowed -w ' + $Width + ' -h ' + $Height + ' -novid -condebug'
    $process = Start-Process -FilePath (Join-Path $SdkRoot 'hl2_win64.exe') -ArgumentList $arguments -WorkingDirectory $runtime -PassThru
    @{game_pid=$process.Id; app_id=[int]$identity} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $repoRoot 'artifacts/menu-preview-process.json')
    Write-Output "Launched mod: PID $($process.Id), AppID $identity"
} finally {
    $env:SteamAppId = $previousApp
    $env:SteamGameId = $previousGame
}

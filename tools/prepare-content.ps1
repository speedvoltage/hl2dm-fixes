[CmdletBinding()]
param([string]$RetailRoot = '')
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
if (-not $RetailRoot) {
    $steamRoot = (Get-ItemProperty -LiteralPath 'HKCU:\Software\Valve\Steam').SteamPath
    $libraries = @($steamRoot)
    $folders = Join-Path $steamRoot 'steamapps/libraryfolders.vdf'
    if (Test-Path -LiteralPath $folders) {
        foreach ($match in [regex]::Matches((Get-Content -Raw -LiteralPath $folders), '"path"\s+"([^"]+)"')) {
            $libraries += $match.Groups[1].Value.Replace('\\', '\')
        }
    }
    foreach ($library in ($libraries | Select-Object -Unique)) {
        $candidate = Join-Path $library 'steamapps/common/Half-Life 2 Deathmatch'
        if (Test-Path -LiteralPath (Join-Path $candidate 'hl2mp/maps/dm_lockdown.bsp')) { $RetailRoot = $candidate; break }
    }
}
if (-not $RetailRoot) { throw 'Install Half-Life 2: Deathmatch, or supply -RetailRoot.' }
$target = Join-Path $repoRoot 'game/mod_hl2mp/maps'
New-Item -ItemType Directory -Force -Path $target | Out-Null
foreach ($map in @('dm_lockdown', 'dm_overwatch', 'dm_powerhouse', 'dm_resistance', 'dm_runoff', 'dm_steamlab', 'dm_underpass')) {
    $source = Join-Path $RetailRoot "hl2mp/maps/$map.bsp"
    if (-not (Test-Path -LiteralPath $source)) { throw "Missing retail map: $source" }
    $destination = Join-Path $target "$map.bsp"
    if (Test-Path -LiteralPath $destination) {
        if ((Get-FileHash -LiteralPath $source).Hash -ne (Get-FileHash -LiteralPath $destination).Hash) {
            Write-Warning "Keeping the existing local map: $destination"
        }
    } else { Copy-Item -LiteralPath $source -Destination $destination }
}
Write-Output "Retail maps are available in $target"

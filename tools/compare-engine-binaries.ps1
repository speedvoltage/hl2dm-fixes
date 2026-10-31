[CmdletBinding()]
param(
    [string]$Retail = 'C:\Program Files (x86)\Steam\steamapps\common\Half-Life 2 Deathmatch',
    [string]$SDK = 'C:\Program Files (x86)\Steam\steamapps\common\Source SDK Base 2013 Multiplayer'
)
$ErrorActionPreference = 'Stop'
$outputDir = Join-Path (Split-Path -Parent $PSScriptRoot) 'artifacts\engine-comparison'
New-Item -ItemType Directory -Path $outputDir -Force | Out-Null
$files = @{}
foreach ($root in @($Retail, $SDK)) {
    Get-ChildItem -LiteralPath (Join-Path $root 'bin') -File -Recurse |
        Where-Object Extension -in '.dll','.exe' |
        ForEach-Object { $files[$_.FullName.Substring($root.Length + 1)] = $true }
}
$pairs = @($files.Keys | Sort-Object | ForEach-Object { @{Name=$_; Retail=$_; SDK=$_} })
$pairs += @{Name='client launcher (64-bit)'; Retail='hl2mp_win64.exe'; SDK='hl2_win64.exe'}
$pairs += @{Name='client launcher (32-bit)'; Retail='hl2mp.exe'; SDK='hl2.exe'}
$pairs += @{Name='dedicated launcher'; Retail='srcds_win64.exe'; SDK='srcds_win64.exe'}
$results = foreach ($pair in $pairs) {
    $retailFile = Join-Path $Retail $pair.Retail
    $sdkFile = Join-Path $SDK $pair.SDK
    $retailHash = if (Test-Path -LiteralPath $retailFile) { (Get-FileHash -LiteralPath $retailFile -Algorithm SHA256).Hash } else { '' }
    $sdkHash = if (Test-Path -LiteralPath $sdkFile) { (Get-FileHash -LiteralPath $sdkFile -Algorithm SHA256).Hash } else { '' }
    $status = if (-not $retailHash) { 'SDK only' } elseif (-not $sdkHash) { 'Retail only' } elseif ($retailHash -eq $sdkHash) { 'Identical' } else { 'Different' }
    [pscustomobject]@{ Binary=$pair.Name; Status=$status; RetailSHA256=$retailHash; SDKSHA256=$sdkHash }
}
$results | Export-Csv -LiteralPath (Join-Path $outputDir 'windows-binaries.csv') -NoTypeInformation -Encoding UTF8
$results | Group-Object Status | Select-Object Name,Count | Format-Table
$results | Where-Object Binary -match 'engine.dll|GameUI.dll|vgui2.dll|vguimatsurface.dll|vphysics.dll|launcher' | Select-Object Binary,Status | Format-Table
Write-Output (Join-Path $outputDir 'windows-binaries.csv')

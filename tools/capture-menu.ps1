[CmdletBinding()]
param([string]$Name = 'menu')
$ErrorActionPreference = 'Stop'
if ($Name -notmatch '^[a-zA-Z0-9_-]+$') { throw 'Use a simple screenshot name.' }
$repoRoot = Split-Path -Parent $PSScriptRoot
$modPath = Join-Path $repoRoot 'game\mod_hl2mp'
Add-Type @'
using System;
using System.Text;
using System.Runtime.InteropServices;
public static class HL2MPCaptureWindow {
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetClassName(IntPtr window, StringBuilder name, int count);
}
'@
# Source forwards -hijack commands to the first Valve001 window. Require exactly
# one such game, and verify that it belongs to this checkout before forwarding.
$sourceGames = @(Get-Process | Where-Object {
    if ($_.MainWindowHandle -eq 0) { return $false }
    $windowClass = New-Object System.Text.StringBuilder(256)
    [HL2MPCaptureWindow]::GetClassName($_.MainWindowHandle, $windowClass, 256) | Out-Null
    return $windowClass.ToString() -eq 'Valve001'
})
if ($sourceGames.Count -ne 1) { throw 'Keep only this mod running before capturing.' }
$game = $sourceGames[0]
$gameInfo = Get-CimInstance Win32_Process -Filter "ProcessId = $($game.Id)"
$modArgument = '(?:^|\s)-game\s+(?:"' + [Regex]::Escape($modPath) + '"|' + [Regex]::Escape($modPath) + ')(?:\s|$)'
if ($game.ProcessName -ne 'hl2_win64' -or -not $gameInfo.CommandLine -or
    $gameInfo.CommandLine.Replace('/', '\') -notmatch $modArgument) {
    throw 'Launch this checkout of the mod before capturing.'
}
$captureName = 'menu_capture_' + [Guid]::NewGuid().ToString('N')
$sourcePath = Join-Path $modPath "screenshots\$captureName.jpg"
$forwardArguments = '-hijack -game "' + $modPath + '" +jpeg ' + $captureName + ' 95'
$forwarder = Start-Process -FilePath $game.Path -ArgumentList $forwardArguments -WorkingDirectory (Split-Path -Parent $game.Path) -WindowStyle Hidden -PassThru
if (-not $forwarder.WaitForExit(5000)) {
    Stop-Process -Id $forwarder.Id
    throw 'The screenshot command could not be forwarded.'
}
if ($forwarder.ExitCode -ne 0) { throw 'The screenshot command failed.' }
$deadline = [DateTime]::UtcNow.AddSeconds(5)
$captureReady = $false
do {
    if (Test-Path -LiteralPath $sourcePath) {
        # The renderer may still be writing. Wait for exclusive read access.
        try {
            $captureStream = [IO.File]::Open($sourcePath, 'Open', 'Read', 'None')
            $captureReady = $captureStream.Length -gt 0
            $captureStream.Dispose()
            if ($captureReady) { break }
        } catch [IO.IOException] {}
    }
    Start-Sleep -Milliseconds 100
} while ([DateTime]::UtcNow -lt $deadline)
if (-not $captureReady) { throw 'The game did not produce a complete screenshot.' }
$outputDir = Join-Path $repoRoot 'artifacts\screenshots'
New-Item -ItemType Directory -Path $outputDir -Force | Out-Null
$outputPath = Join-Path $outputDir ((Get-Date -Format 'yyyyMMdd-HHmmss') + '-' + $Name + '.jpg')
Copy-Item -LiteralPath $sourcePath -Destination $outputPath
Write-Output $outputPath

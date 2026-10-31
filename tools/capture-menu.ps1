[CmdletBinding()]
param([string]$Name = 'menu')
$ErrorActionPreference = 'Stop'
if ($Name -notmatch '^[a-zA-Z0-9_-]+$') { throw 'Use a simple screenshot name.' }
Add-Type -AssemblyName System.Drawing
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class HL2MPWindowCapture {
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
    [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X, Y; }
    [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr window, out RECT rect);
    [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr window, ref POINT point);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr window);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr window, int command);
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
}
'@
$game = Get-Process hl2_win64 -ErrorAction Stop | Where-Object MainWindowHandle -ne 0 | Select-Object -First 1
if (-not $game) { throw 'Launch the mod in windowed mode before capturing.' }
[HL2MPWindowCapture]::ShowWindow($game.MainWindowHandle, 9) | Out-Null
$previousWindow = [HL2MPWindowCapture]::GetForegroundWindow()
[HL2MPWindowCapture]::SetForegroundWindow($game.MainWindowHandle) | Out-Null
Start-Sleep -Milliseconds 400
if ([HL2MPWindowCapture]::GetForegroundWindow() -ne $game.MainWindowHandle) {
    throw 'Bring the mod window to the foreground, then capture again.'
}
$rect = New-Object HL2MPWindowCapture+RECT
if (-not [HL2MPWindowCapture]::GetClientRect($game.MainWindowHandle, [ref]$rect)) { throw 'Cannot read game window bounds.' }
$origin = New-Object HL2MPWindowCapture+POINT
if (-not [HL2MPWindowCapture]::ClientToScreen($game.MainWindowHandle, [ref]$origin)) { throw 'Cannot locate game canvas.' }
$outputDir = Join-Path (Split-Path -Parent $PSScriptRoot) 'artifacts\screenshots'
New-Item -ItemType Directory -Path $outputDir -Force | Out-Null
$outputPath = Join-Path $outputDir ((Get-Date -Format 'yyyyMMdd-HHmmss') + '-' + $Name + '.png')
$bitmap = New-Object System.Drawing.Bitmap(($rect.Right - $rect.Left), ($rect.Bottom - $rect.Top))
$graphics = [System.Drawing.Graphics]::FromImage($bitmap)
try {
    # Direct3D may return a black surface to PrintWindow once initialization ends.
    # Capture only the foreground game window, never the surrounding desktop.
    $graphics.CopyFromScreen($origin.X, $origin.Y, 0, 0, $bitmap.Size)
    $bitmap.Save($outputPath, [System.Drawing.Imaging.ImageFormat]::Png)
}
finally {
    $graphics.Dispose(); $bitmap.Dispose()
    if ($previousWindow -ne [IntPtr]::Zero) { [HL2MPWindowCapture]::SetForegroundWindow($previousWindow) | Out-Null }
}
Write-Output $outputPath

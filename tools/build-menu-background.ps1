[CmdletBinding()]
param(
    [string]$SdkRoot = 'C:\Program Files (x86)\Steam\steamapps\common\Source SDK Base 2013 Multiplayer',
    [string]$SourceImage = '',
    [ValidatePattern('^[a-z0-9_]+$')][string]$TextureName = 'dm_lockdown'
)
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$source = if ($SourceImage) { $SourceImage } else { Join-Path $repoRoot 'assets/menu/dm_lockdown.jpg' }
$staging = Join-Path $repoRoot 'artifacts/menu-texture'
$output = Join-Path $repoRoot 'game/mod_hl2mp/materials/vgui/matchmaking'
New-Item -ItemType Directory -Force -Path $staging, $output | Out-Null

# VTF uses power-of-two dimensions; the menu restores the artwork's 16:9 aspect.
Add-Type -AssemblyName System.Drawing
Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition @'
using System;
using System.Drawing;
using System.Drawing.Imaging;
using System.Drawing.Drawing2D;
using System.IO;
using System.Runtime.InteropServices;
public static class MenuTextureSource {
    public static void Write(string source, string target) {
        using (var original = Image.FromFile(source))
        using (var bitmap = new Bitmap(2048, 1024, PixelFormat.Format24bppRgb)) {
            using (var graphics = Graphics.FromImage(bitmap)) {
                graphics.InterpolationMode = InterpolationMode.HighQualityBicubic;
                graphics.DrawImage(original, 0, 0, bitmap.Width, bitmap.Height);
            }
            var data = bitmap.LockBits(new Rectangle(0, 0, bitmap.Width, bitmap.Height), ImageLockMode.ReadOnly, PixelFormat.Format24bppRgb);
            try {
                using (var writer = new BinaryWriter(File.Create(target))) {
                    var header = new byte[18];
                    header[2] = 2; header[13] = 8; header[15] = 4; header[16] = 24; header[17] = 32;
                    writer.Write(header);
                    var row = new byte[bitmap.Width * 3];
                    for (int y = 0; y < bitmap.Height; y++) {
                        Marshal.Copy(IntPtr.Add(data.Scan0, y * data.Stride), row, 0, row.Length);
                        writer.Write(row);
                    }
                }
            } finally { bitmap.UnlockBits(data); }
        }
    }
}
'@
$tga = Join-Path $staging ($TextureName + '.tga')
[MenuTextureSource]::Write($source, $tga)
@'
"nolod" "1"
"nomip" "1"
"clamps" "1"
"clampt" "1"
'@ | Set-Content -LiteralPath (Join-Path $staging ($TextureName + '.txt')) -Encoding Ascii
& (Join-Path $SdkRoot 'bin/x64/vtex.exe') -nopause -nop4 -game (Join-Path $repoRoot 'game/mod_hl2mp') -outdir $output $tga
if ($LASTEXITCODE -ne 0) { throw 'Menu texture compilation failed.' }
if (-not (Test-Path -LiteralPath (Join-Path $output ($TextureName + '.vtf')))) { throw 'VTEX did not produce the menu texture.' }

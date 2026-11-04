"""Rasterize the editable SVG killfeed icon and compile its Source texture.

Install tools/asset-requirements.txt first. Requires the Windows SDK VTEX tool.
"""
import argparse
import io
import os
from pathlib import Path
import subprocess
import resvg_py
from PIL import Image

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sdk-root', type=Path, default=Path(os.environ.get('ProgramFiles(x86)', 'C:/Program Files (x86)')) / 'Steam/steamapps/common/Source SDK Base 2013 Multiplayer')
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    staging = root / 'artifacts/hud-icons'
    staging.mkdir(parents=True, exist_ok=True)
    output = root / 'game/mod_hl2mp/materials/vgui/hud'
    output.mkdir(parents=True, exist_ok=True)
    for name, height in [('death_skull', 64), ('death_headshot', 64), ('death_airborne', 32)]:
        data = resvg_py.svg_to_bytes(svg_path=str(root / 'assets/icons' / (name.replace('_', '-') + '.svg')), width=256, height=height*4)
        icon = Image.open(io.BytesIO(data)).convert('RGBA').resize((64, height), Image.Resampling.LANCZOS)
        icon.save(staging / (name + '.png'))
        icon.save(staging / (name + '.tga'))
        (staging / (name + '.txt')).write_text('"nolod" "1"\n"nomip" "1"\n"nocompress" "1"\n"clamps" "1"\n"clampt" "1"\n')
        subprocess.run([str(args.sdk_root / 'bin/x64/vtex.exe'), '-nopause', '-nop4', '-game', str(root / 'game/mod_hl2mp'), '-outdir', str(output), str(staging / (name + '.tga'))], check=True)
        assert (output / (name + '.vtf')).exists()
        (output / (name + '.vmt')).write_text('"UnlitGeneric"\n{\n    "$basetexture" "vgui/hud/' + name + '"\n    "$translucent" "1"\n    "$vertexcolor" "1"\n    "$vertexalpha" "1"\n    "$ignorez" "1"\n    "$nolod" "1"\n}\n')

if __name__ == '__main__':
    main()

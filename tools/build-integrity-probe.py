"""Build the tiny VPK used to test the engine's live pure-file tracker."""
from pathlib import Path
import struct
import zlib

PAYLOAD = b"HL2MP integrity tracking probe v1.\n"

def build_probe():
    entry = struct.pack("<IHHIIH", zlib.crc32(PAYLOAD), 0, 0x7fff, 0, len(PAYLOAD), 0xffff)
    tree = b"txt\0 \0hl2mp_integrity_probe\0" + entry + b"\0\0\0"
    return struct.pack("<III", 0x55aa1234, 1, len(tree)) + tree + PAYLOAD

if __name__ == "__main__":
    path = Path(__file__).resolve().parents[1] / "game/mod_hl2mp/resource/hl2mp_integrity_dir.vpk"
    path.write_bytes(build_probe())
    print(path)

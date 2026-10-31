"""Assemble a separate 64-bit SDK server runtime from official local Steam files.

The public dedicated-server install currently omits the 64-bit launcher and
several dependencies. Supply those files from the matching SDK Linux depot and
a Linux SteamCMD install. This copies files and creates aliases; it patches no
engine binaries and never changes the source installations.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil


def elf64(path):
    with path.open("rb") as stream:
        if stream.read(5) != b"\x7fELF\x02":
            raise ValueError(f"Expected a 64-bit ELF file: {path}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--server-install", type=Path, required=True)
    parser.add_argument("--sdk-linux-depot", type=Path, required=True)
    parser.add_argument("--steamclient", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if os.name != "posix":
        parser.error("Run this tool on Linux (or inside WSL)")
    sources = {name: args.sdk_linux_depot / name for name in
               ("srcds_linux64", "bin/linux64/filesystem_stdio.so", "bin/linux64/libsteam_api.so")}
    sources["bin/linux64/steamclient.so"] = args.steamclient
    for source in sources.values():
        elf64(source)
    elf64(args.server_install / "bin/linux64/engine_srv.so")
    if args.output.exists():
        parser.error("Choose a new output directory; existing runtimes are never overwritten")
    shutil.copytree(args.server_install, args.output)
    manifest = {}
    for name, source in sources.items():
        target = args.output / name
        shutil.copy2(source, target)
        manifest[name] = {"source": str(source.resolve()), "sha256": hashlib.sha256(target.read_bytes()).hexdigest()}
    for library in (args.output / "bin/linux64").glob("*_srv.so"):
        alias = library.with_name(library.name.replace("_srv.so", ".so"))
        if alias.exists() or alias.is_symlink():
            raise ValueError(f"Unexpected existing library alias: {alias}")
        alias.symlink_to(library.name)
    (args.output / "srcds_linux64").chmod(0o755)
    (args.output / "steam_appid.txt").write_text("243750\n")
    (args.output / "runtime-inputs.json").write_text(json.dumps(manifest, indent=2))
    print(args.output.resolve())


if __name__ == "__main__":
    main()

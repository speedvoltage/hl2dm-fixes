"""Package the built mod and tracked assets without local settings or SDK files."""
import argparse
import gzip
import hashlib
import io
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tarfile
import tempfile
import zipfile


def check_binary(path, platform):
    with path.open("rb") as stream:
        header = stream.read(64)
        if platform == "linux":
            valid = header[:6] == b"\x7fELF\x02\x01" and struct.unpack_from("<H", header, 18)[0] == 62
        else:
            valid = header[:2] == b"MZ"
            if valid:
                stream.seek(struct.unpack_from("<I", header, 60)[0])
                valid = stream.read(6) == b"PE\0\0\x64\x86"
    if not valid:
        raise ValueError(f"Expected a 64-bit {platform} binary: {path}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--platform", choices=("windows", "linux"), required=True)
    parser.add_argument("--output", type=Path, help="New archive path; existing files are never overwritten")
    parser.add_argument("--git", default="git", help="Git executable")
    parser.add_argument("--strip", default="strip", help="Linux strip executable (only the packaged copies are stripped)")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]

    def git(*arguments):
        return subprocess.check_output([args.git, *arguments], cwd=root)

    # Identify the exact source revision represented by the package. Tooling can
    # be edited independently, but game code and assets must match that revision.
    git("diff", "--exit-code", "HEAD", "--", "src", "game/mod_hl2mp")
    revision = git("rev-parse", "HEAD").decode().strip()
    extension = ".zip" if args.platform == "windows" else ".tar.gz"
    output = args.output or root / "artifacts/packages" / f"hl2dm-matchmaking-{revision[:8]}-{args.platform}{extension}"
    if output.exists():
        parser.error("Choose a new archive path; existing packages are never overwritten")
    if args.platform == "linux" and os.name != "posix":
        parser.error("Create Linux packages on Linux or inside WSL")

    tracked = git("ls-files", "-z", "--", "game/mod_hl2mp").decode("utf-8").split("\0")
    binaries = ("bin/x64/client.dll", "bin/x64/server.dll") if args.platform == "windows" else ("bin/linux64/client.so", "bin/linux64/server.so")
    for name in binaries:
        check_binary(root / "game/mod_hl2mp" / name, args.platform)

    with tempfile.TemporaryDirectory(prefix="hl2dm-package-") as temporary:
        staging = Path(temporary) / "hl2dm_matchmaking"
        staging.mkdir()
        for name in filter(None, tracked):
            source = root / name
            destination = staging / source.relative_to(root / "game/mod_hl2mp")
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(source, destination)
        shutil.copyfile(root / "LICENSE", staging / "LICENSE")
        for name in binaries:
            destination = staging / name
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(root / "game/mod_hl2mp" / name, destination)
            if args.platform == "linux":
                subprocess.run([args.strip, "--strip-debug", str(destination)], check=True)
            check_binary(destination, args.platform)
        if args.platform == "linux":
            (staging / "bin/linux64/server_srv.so").symlink_to("server.so")

        manifest = {"source_commit": revision, "platform": args.platform, "architecture": "x86_64",
                    "sdk_app_id": 243750, "matchmaking_protocol": 1, "files": {}}
        for path in sorted(staging.rglob("*")):
            name = path.relative_to(staging).as_posix()
            if path.is_symlink():
                manifest["files"][name] = {"link": os.readlink(path)}
            elif path.is_file():
                manifest["files"][name] = {"sha256": hashlib.sha256(path.read_bytes()).hexdigest(), "bytes": path.stat().st_size}
        (staging / "package.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")

        # Use fixed timestamps and permissions so identical inputs produce the
        # same archive, regardless of the build machine's local file metadata.
        output.parent.mkdir(parents=True, exist_ok=True)
        with output.open("xb") as target:
            if args.platform == "windows":
                with zipfile.ZipFile(target, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
                    for path in sorted(staging.rglob("*")):
                        if path.is_file():
                            entry = zipfile.ZipInfo(path.relative_to(staging.parent).as_posix(), (2000, 1, 1, 0, 0, 0))
                            entry.create_system = 3
                            entry.external_attr = 0o100644 << 16
                            entry.compress_type = zipfile.ZIP_DEFLATED
                            archive.writestr(entry, path.read_bytes(), compresslevel=9)
            else:
                with gzip.GzipFile(filename="", mode="wb", fileobj=target, mtime=0) as compressed:
                    with tarfile.open(fileobj=compressed, mode="w") as archive:
                        for path in sorted(staging.rglob("*")):
                            if path.is_dir():
                                continue
                            entry = tarfile.TarInfo(path.relative_to(staging.parent).as_posix())
                            entry.mode = 0o755 if path.suffix == ".so" else 0o644
                            if path.is_symlink():
                                entry.type = tarfile.SYMTYPE
                                entry.linkname = os.readlink(path)
                                archive.addfile(entry)
                            else:
                                data = path.read_bytes()
                                entry.size = len(data)
                                archive.addfile(entry, io.BytesIO(data))
        print(output.resolve())
        print("SHA256 " + hashlib.sha256(output.read_bytes()).hexdigest())


if __name__ == "__main__":
    main()

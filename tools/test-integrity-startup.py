"""Native Windows regression checks for gameinfo/pure-tracking startup failures.

Requires built client binaries and SDK content. Close the mod first. Each case
temporarily changes an installation file, launches its own client, checks the
engine error dialog, and restores the original files in finally.
"""
import argparse
import ctypes as c
from ctypes import wintypes as w
import json
import os
from pathlib import Path
import re
import subprocess
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    root = Path(__file__).resolve().parents[1]
    parser.add_argument("--sdk-root", type=Path, default=Path(os.environ.get("ProgramFiles(x86)", "C:/Program Files (x86)")) / "Steam/steamapps/common/Source SDK Base 2013 Multiplayer")
    parser.add_argument("--mod-root", type=Path, default=root / "game/mod_hl2mp")
    args = parser.parse_args()
    if os.name != "nt":
        parser.error("This integration test uses the native Windows engine.")
    if subprocess.run(["powershell", "-NoProfile", "-Command", "if (Get-Process -Name hl2_win64 -ErrorAction SilentlyContinue) { exit 1 }"], capture_output=True).returncode:
        parser.error("Close the running Source client before testing.")
    mod = args.mod_root.resolve()
    info = mod / "gameinfo.txt"
    original = info.read_bytes()
    app = re.search(rb"SteamAppId\s+(\d+)", original).group(1).decode()
    assert b"type multiplayer_only" in original, "Start with the unmodified gameinfo.txt"
    probe = mod / "resource/hl2mp_integrity_dir.vpk"
    probe_bytes = probe.read_bytes()
    shadow = mod / "custom/integrity_regression/gameinfo.txt"
    assert not shadow.exists(), "Refusing to overwrite an existing custom gameinfo.txt"
    runtime = root / "artifacts/integrity-test-runtime"
    runtime.mkdir(parents=True, exist_ok=True)
    (runtime / "steam_appid.txt").write_text(app + "\n")
    user32 = c.WinDLL("user32", use_last_error=True)
    user32.GetWindowThreadProcessId.argtypes = [w.HWND, c.POINTER(w.DWORD)]
    user32.GetWindowTextW.argtypes = [w.HWND, w.LPWSTR, c.c_int]
    callback = c.WINFUNCTYPE(w.BOOL, w.HWND, w.LPARAM)
    results = []
    reasons = {
        "singleplayer": "Restore type multiplayer_only",
        "missing_type": "Restore type multiplayer_only",
        "shadow": "search-path override",
        "missing_probe": "integrity resource is missing",
    }
    for case, reason in reasons.items():
        proc = None
        try:
            if case == "singleplayer":
                info.write_bytes(original.replace(b"type multiplayer_only", b"type singleplayer_only"))
            elif case == "missing_type":
                info.write_bytes(original.replace(b"type multiplayer_only", b""))
            elif case == "shadow":
                shadow.parent.mkdir(parents=True, exist_ok=True)
                shadow.write_text('"GameInfo" { "type" "singleplayer_only" }\n')
            else:
                probe.unlink()
            startup = subprocess.STARTUPINFO()
            startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
            startup.wShowWindow = subprocess.SW_HIDE
            proc = subprocess.Popen([str(args.sdk_root / "hl2_win64.exe"), "-steam", "-game", str(mod), "-windowed", "-w", "800", "-h", "600", "-novid", "+connect", "127.0.0.1:27099"], cwd=runtime, env=dict(os.environ, SteamAppId=app, SteamGameId=app), startupinfo=startup)
            texts = []

            @callback
            def child(handle, unused):
                text = c.create_unicode_buffer(2048)
                user32.GetWindowTextW(handle, text, len(text))
                if text.value:
                    texts.append(text.value)
                return True

            @callback
            def top(handle, unused):
                pid = w.DWORD()
                user32.GetWindowThreadProcessId(handle, c.byref(pid))
                if pid.value == proc.pid:
                    child(handle, 0)
                    user32.EnumChildWindows(handle, child, 0)
                return True

            deadline = time.monotonic() + 25
            while time.monotonic() < deadline and proc.poll() is None:
                texts.clear()
                user32.EnumWindows(top, 0)
                if any("cannot start safely" in text for text in texts):
                    break
                time.sleep(0.1)
            assert any("cannot start safely" in text for text in texts), (case, texts)
            assert any(reason in text for text in texts), (case, texts)
            results.append({"case": case, "rejected": True, "dialog": list(texts)})
            print(case + ": rejected", flush=True)
        finally:
            if proc and proc.poll() is None:
                proc.terminate()
                proc.wait(timeout=10)
            info.write_bytes(original)
            probe.write_bytes(probe_bytes)
            shadow.unlink(missing_ok=True)
    (root / "artifacts/integrity-startup-tests.json").write_text(json.dumps(results, indent=2) + "\n")


if __name__ == "__main__":
    main()

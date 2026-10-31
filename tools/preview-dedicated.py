"""Exercise the real local SRCDS agent and client with a development API login.

Steam still validates the player's game-server identity. The coordinator login
uses the explicit loopback development provider until the mod owns a Steam app.
"""
import argparse
import json
import os
from pathlib import Path
import secrets
import shutil
import socket
import sqlite3
import subprocess
import sys
import time

import httpx


def console_command(process, command):
    """Send a command to the hidden console of our own SRCDS child."""
    import ctypes as c
    from ctypes import wintypes as w
    if process.poll() is not None:
        raise RuntimeError("The preview server has stopped")
    kernel = c.WinDLL("kernel32", use_last_error=True)

    class Key(c.Structure):
        _fields_ = [("down", w.BOOL), ("repeat", w.WORD), ("virtual", w.WORD),
                    ("scan", w.WORD), ("char", w.WCHAR), ("control", w.DWORD)]

    class Event(c.Union):
        _fields_ = [("key", Key), ("padding", c.c_byte * 16)]

    class Input(c.Structure):
        _fields_ = [("kind", w.WORD), ("event", Event)]

    kernel.FreeConsole()
    if not kernel.AttachConsole(process.pid):
        raise OSError(c.get_last_error(), "Could not attach to the preview console")
    kernel.CreateFileW.restype = w.HANDLE
    kernel.WriteConsoleInputW.argtypes = [w.HANDLE, c.POINTER(Input), w.DWORD, c.POINTER(w.DWORD)]
    kernel.CloseHandle.argtypes = [w.HANDLE]
    handle = kernel.CreateFileW("CONIN$", 0x40000000, 3, None, 3, 0, None)
    try:
        text = command + "\r"
        events = (Input * len(text))()
        for i, char in enumerate(text):
            events[i].kind = 1
            events[i].event.key = Key(1, 1, 13 if char == "\r" else ord(char.upper()), 0, char, 0)
        count = w.DWORD()
        if not kernel.WriteConsoleInputW(handle, events, len(text), c.byref(count)):
            raise OSError(c.get_last_error(), "Could not write the preview command")
    finally:
        kernel.CloseHandle(handle)
        kernel.FreeConsole()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--seconds", type=int, default=150)
    parser.add_argument("--server-only", action="store_true")
    checks = parser.add_mutually_exclusive_group()
    checks.add_argument("--verify", action="store_true", help="Verify admission, a one-minute casual round, result reporting, and server release")
    checks.add_argument("--unreserved", action="store_true", help="Verify that a different coordinator identity cannot enter the reserved match")
    args = parser.parse_args()
    if os.name != "nt":
        raise SystemExit("This runtime preview currently targets Windows.")
    if args.server_only and (args.verify or args.unreserved):
        parser.error("Runtime verification requires a game client")
    import winreg
    with winreg.OpenKey(winreg.HKEY_CURRENT_USER, r"Software\Valve\Steam\ActiveProcess") as key:
        account = winreg.QueryValueEx(key, "ActiveUser")[0]
    if not account:
        raise SystemExit("Sign into Steam before running this preview.")
    steam_id = "1" if args.unreserved else str(76561197960265728 + account)
    root = Path(__file__).resolve().parents[1]
    directory = root / "artifacts" / ("dedicated-preview-" + time.strftime("%Y%m%d-%H%M%S"))
    directory.mkdir(parents=True)
    with socket.socket() as listener:
        listener.bind(("127.0.0.1", 0))
        port = listener.getsockname()[1]
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as listener:
        for candidate in range(27025, 27050):
            try:
                listener.bind(("127.0.0.1", candidate))
                break
            except OSError:
                continue
        else:
            raise RuntimeError("No preview server port is available in 27025-27049")
        game_port = listener.getsockname()[1]
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as client_listener:
            client_listener.bind(("127.0.0.1", 0))
            client_port = client_listener.getsockname()[1]
    url = f"http://127.0.0.1:{port}"
    key, server_key = secrets.token_urlsafe(32), secrets.token_urlsafe(32)
    config = directory / "servers.json"
    config.write_text(json.dumps([dict(id="local-srcds", token=server_key, address="127.0.0.1", port=game_port, region="eu", capacity=16, modes=["casual"])]))
    sdk = Path(os.environ.get("ProgramFiles(x86)", "C:/Program Files (x86)")) / "Steam/steamapps/common/Source SDK Base 2013 Multiplayer"
    children, logs = [], []
    server_log = root / "game/mod_hl2mp/matchmaking-server.log"
    log_offset = server_log.stat().st_size if server_log.exists() else 0
    verified = False

    def launch(argv, name, env, cwd=root, hidden=True):
        if name == "srcds":
            # SRCDS reads Windows console input directly. Redirected/absent stdin
            # makes GetNumberOfConsoleInputEvents fail; give it a hidden console.
            startup = subprocess.STARTUPINFO()
            startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
            startup.wShowWindow = subprocess.SW_HIDE
            child = subprocess.Popen(argv, cwd=cwd, env=env, startupinfo=startup,
                                     creationflags=subprocess.CREATE_NEW_CONSOLE)
            children.append(child)
            return child
        log = (directory / (name + ".log")).open("w")
        logs.append(log)
        child = subprocess.Popen(argv, cwd=cwd, env=env, stdout=log, stderr=subprocess.STDOUT,
                                 creationflags=subprocess.CREATE_NO_WINDOW if hidden else 0)
        children.append(child)
        return child

    try:
        service = launch([sys.executable, "-m", "services.coordinator", "--development", "--port", str(port), "--servers", str(config), "--database", str(directory / "state.sqlite3")], "coordinator", dict(os.environ, MM_DEVELOPMENT_KEY=key))
        with httpx.Client(base_url=url, timeout=5) as client:
            for _ in range(50):
                if service.poll() is not None:
                    raise RuntimeError("Coordinator stopped; inspect its log")
                try:
                    if client.get("/v1/health").status_code == 200:
                        break
                except httpx.TransportError:
                    pass
                time.sleep(0.2)
            else:
                raise RuntimeError("Coordinator did not become ready")
            response = client.post("/v1/auth/development", json={"steam_id": steam_id, "key": key})
            response.raise_for_status()
            token = response.json()["token"]
            server = launch([str(sdk / "srcds_win64.exe"), "-console", "-game", str(root / "game/mod_hl2mp"), "-ip", "127.0.0.1", "-port", str(game_port), "-maxplayers", "16", "-mm_server", "-condebug", "+con_logfile", "matchmaking-server.log", "+net_usesocketsforloopback", "1", "+sv_lan", "0", "+map", "dm_lockdown"], "srcds", dict(os.environ, HL2MP_MM_SERVER_URL=url, HL2MP_MM_SERVER_TOKEN=server_key), cwd=sdk)
            game = None
            if not args.server_only:
                game = launch([str(sdk / "hl2_win64.exe"), "-steam", "-game", str(root / "game/mod_hl2mp"), "-ip", "127.0.0.1", "+clientport", str(client_port), "+net_usesocketsforloopback", "1", "-windowed", "-w", "1280", "-h", "720", "-novid", "-condebug", "+con_logfile", "matchmaking-client.log", "-mm_local_test", "+mm_queue", "casual"], "client", dict(os.environ, HL2MP_MM_DEV_URL=url, HL2MP_MM_DEV_SESSION=token), cwd=sdk, hidden=False)
            (directory / "processes.json").write_text(json.dumps({"service_pid": service.pid, "server_pid": server.pid, "game_pid": game.pid if game else None, "url": url, "game_port": game_port}))
            print(f"Dedicated preview: {directory}; server PID {server.pid}; client PID {game.pid if game else None}", flush=True)
            headers = {"Authorization": "Bearer " + token}
            start = time.time()
            last_state, copied = None, False
            shortened = False
            while time.time() - start < args.seconds:
                if service.poll() is not None or server.poll() is not None or (game and game.poll() is not None):
                    print("A preview process exited.", flush=True)
                    break
                response = client.get("/v1/me", headers=headers)
                response.raise_for_status()
                state = response.json()
                match = state.get("match")
                phase = match["state"] if match else ("queued" if state.get("queue") else "idle")
                if phase != last_state:
                    print("Player state: " + phase, flush=True)
                    evidence = {k: state.get(k) for k in ("queue", "match", "last_match")}
                    if match and match.get("connection"):
                        match["connection"].pop("join_token", None)
                    (directory / (phase + ".json")).write_text(json.dumps(evidence, indent=2))
                    last_state = phase
                screenshot = root / "game/mod_hl2mp/screenshots/matchmaking_gameplay.jpg"
                if not copied and screenshot.exists() and screenshot.stat().st_mtime >= start:
                    output = root / "artifacts/screenshots" / (time.strftime("%Y%m%d-%H%M%S") + "-dedicated-gameplay.jpg")
                    output.parent.mkdir(parents=True, exist_ok=True)
                    shutil.copyfile(screenshot, output)
                    copied = True
                    print(f"In-game screenshot: {output}", flush=True)
                log_text = ""
                if server_log.exists():
                    with server_log.open("rb") as stream:
                        stream.seek(log_offset)
                        log_text = stream.read().decode("utf-8", errors="replace")
                if args.unreserved and "Matchmaking_reservation_ended" in log_text and "engine authenticated player" in log_text:
                    if "admitted player" in log_text:
                        raise RuntimeError("An unreserved player was admitted")
                    proof = {"check": "unreserved_player_rejected", "success": True}
                    (directory / "verification.json").write_text(json.dumps(proof, indent=2))
                    print("Verified: an authenticated Steam player with a different coordinator identity was rejected.", flush=True)
                    verified = True
                    break
                if args.verify and copied and not shortened:
                    console_command(server, "mp_timelimit 1")
                    shortened = True
                last = state.get("last_match") or {}
                if args.verify and last.get("state") == "finished":
                    if "admitted player" not in log_text or "match started" not in log_text:
                        raise RuntimeError("Result arrived without native admission and gameplay")
                    if last["result"] != {"winner": None, "rating_change_team_0": 0}:
                        raise RuntimeError("Casual match unexpectedly affected ratings")
                    with sqlite3.connect(f"file:{(directory / 'state.sqlite3').as_posix()}?mode=ro", uri=True) as db:
                        stored = json.loads(db.execute("SELECT payload FROM state WHERE id=1").fetchone()[0])
                    if stored["servers"]["local-srcds"]["match"] is None:
                        proof = {"check": "native_casual_lifecycle", "success": True, "result": last["result"], "server_released": True}
                        (directory / "verification.json").write_text(json.dumps(proof, indent=2))
                        print("Verified: native admission, gameplay, result reporting, and server release.", flush=True)
                        verified = True
                        break
                time.sleep(1)
            if (args.verify or args.unreserved) and not verified:
                raise RuntimeError("The runtime check did not complete; inspect the evidence directory")
    finally:
        for child in reversed(children):
            if child.poll() is None:
                child.terminate()
                try:
                    child.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    child.kill()
                    child.wait()
        for log in logs:
            log.close()
        for name in ("matchmaking-server.log", "matchmaking-client.log"):
            source = root / "game/mod_hl2mp" / name
            if source.exists():
                shutil.copyfile(source, directory / name)


if __name__ == "__main__":
    main()

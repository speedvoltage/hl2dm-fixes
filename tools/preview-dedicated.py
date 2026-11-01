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
from matchmaking_faults import ServerFaultProxy



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
    parser.add_argument("--custom", action="store_true", help="Verify a private FFA reservation with explicit rules")
    parser.add_argument("--rounds", type=int, default=1, help="With --verify, play consecutive rounds using the same client and server processes")
    checks = parser.add_mutually_exclusive_group()
    checks.add_argument("--verify", action="store_true", help="Verify admission, a one-minute casual round, result reporting, and server release")
    checks.add_argument("--reconnect", action="store_true", help="Disconnect and rejoin an active casual match before verifying its result")
    checks.add_argument("--unreserved", action="store_true", help="Verify that a different coordinator identity cannot enter the reserved match")
    checks.add_argument("--expired-session", action="store_true", help="Verify bounded server release when the client loses its coordinator session")
    checks.add_argument("--coordinator-outage", choices=("brief", "sustained"), help="Interrupt server API traffic and verify safe recovery or unrated cancellation")
    args = parser.parse_args()
    if args.custom and (not args.verify or args.rounds != 1):
        parser.error("--custom requires --verify and one round")
    if os.name != "nt":
        raise SystemExit("This runtime preview currently targets Windows.")
    if args.server_only and (args.verify or args.reconnect or args.unreserved or args.expired_session or args.coordinator_outage):
        parser.error("Runtime verification requires a game client")
    if not 1 <= args.rounds <= 3 or (args.rounds > 1 and not args.verify):
        parser.error("Use 1-3 rounds; multiple rounds require --verify")
    import winreg
    with winreg.OpenKey(winreg.HKEY_CURRENT_USER, r"Software\Valve\Steam\ActiveProcess") as key:
        account = winreg.QueryValueEx(key, "ActiveUser")[0]
    if not account:
        raise SystemExit("Sign into Steam before running this preview.")
    steam_id = "1" if args.unreserved else str(76561197960265728 + account)
    root = Path(__file__).resolve().parents[1]
    directory = root / "artifacts" / ("dedicated-preview-" + time.strftime("%Y%m%d-%H%M%S"))
    directory.mkdir(parents=True)
    (directory / "steam_appid.txt").write_text("243750\n")
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
    custom_script = root / "game/mod_hl2mp/cfg" / f"mm_private_preview_{port}.cfg"
    if args.custom:
        custom_script.write_text("mm_custom dm_lockdown 0 1 25\n")
    config = directory / "servers.json"
    config.write_text(json.dumps([dict(id="local-srcds", token=server_key, address="127.0.0.1", port=game_port, region="eu", capacity=16, modes=["casual", "custom"])]))
    sdk = Path(os.environ.get("ProgramFiles(x86)", "C:/Program Files (x86)")) / "Steam/steamapps/common/Source SDK Base 2013 Multiplayer"
    children, logs = [], []
    server_log = root / "game/mod_hl2mp/matchmaking-server.log"
    log_offset = server_log.stat().st_size if server_log.exists() else 0
    client_log = root / "game/mod_hl2mp/matchmaking-client.log"
    client_offset = client_log.stat().st_size if client_log.exists() else 0
    verified = False
    fault_proxy = None

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
            if args.coordinator_outage:
                fault_proxy = ServerFaultProxy(url)
            server = launch([str(sdk / "srcds_win64.exe"), "-console", "-game", str(root / "game/mod_hl2mp"), "-ip", "127.0.0.1", "-port", str(game_port), "-maxplayers", "16", "-mm_server", "-condebug", "+con_logfile", "matchmaking-server.log", "+net_usesocketsforloopback", "1", "+sv_lan", "0", "+map", "dm_lockdown"], "srcds", dict(os.environ, HL2MP_MM_SERVER_URL=fault_proxy.url if fault_proxy else url, HL2MP_MM_SERVER_TOKEN=server_key), cwd=directory)
            game = None
            if not args.server_only:
                game = launch([str(sdk / "hl2_win64.exe"), "-steam", "-game", str(root / "game/mod_hl2mp"), "-ip", "127.0.0.1", "+clientport", str(client_port), "+net_usesocketsforloopback", "1", "-windowed", "-w", "1280", "-h", "720", "-novid", "-condebug", "+con_logfile", "matchmaking-client.log", "-mm_local_test", *( ["+mm_menu_background", "1"] if args.custom else ["+mm_queue", "casual"] )], "client", dict(os.environ, HL2MP_MM_DEV_URL=url, HL2MP_MM_DEV_SESSION=token), cwd=directory, hidden=False)
            (directory / "processes.json").write_text(json.dumps({"service_pid": service.pid, "server_pid": server.pid, "game_pid": game.pid if game else None, "url": url, "game_port": game_port}))
            print(f"Dedicated preview: {directory}; server PID {server.pid}; client PID {game.pid if game else None}", flush=True)
            headers = {"Authorization": "Bearer " + token}
            start = time.time()
            round_start = start
            completed_rounds = []
            native_started = None
            last_state, copied = None, False
            custom_requested = False
            shortened = False
            outage_started, outage_recovered = None, False
            reconnect_started, reconnect_requested, rejoined = None, False, False
            reconnect_match = None
            while time.time() - start < args.seconds:
                if service.poll() is not None or server.poll() is not None or (game and game.poll() is not None):
                    print("A preview process exited.", flush=True)
                    break
                response = client.get("/v1/me", headers=headers)
                response.raise_for_status()
                state = response.json()
                if args.custom and not custom_requested and time.time() - start > 12:
                    with sqlite3.connect(f"file:{(directory / 'state.sqlite3').as_posix()}?mode=ro", uri=True) as db:
                        pool = json.loads(db.execute("SELECT payload FROM state WHERE id=1").fetchone()[0])["servers"]
                    if time.time() - pool["local-srcds"]["seen"] > 20:
                        time.sleep(0.2)
                        continue
                    subprocess.run([str(sdk / "hl2_win64.exe"), "-hijack", "-game", str(root / "game/mod_hl2mp"), "+exec", custom_script.name], cwd=directory, check=True, timeout=5, creationflags=subprocess.CREATE_NO_WINDOW)
                    custom_requested = True
                match = state.get("match")
                if args.custom and match and match["mode"] != "custom":
                    raise RuntimeError("Expected a private reservation")
                if args.custom and match and match["state"] == "ready_check":
                    expected_rules = {"team_size": 0, "time_limit": 1, "frag_limit": 25}
                    if match.get("rules") != expected_rules:
                        raise RuntimeError("Private settings did not reach the coordinator")
                    client.post(f"/v1/matches/{match['id']}/confirm", json={"accept": True}, headers=headers).raise_for_status()
                phase = match["state"] if match else ("queued" if state.get("queue") else "idle")
                if phase != last_state:
                    print("Player state: " + phase, flush=True)
                    evidence = {k: state.get(k) for k in ("queue", "match", "last_match")}
                    if match and match.get("connection"):
                        match["connection"].pop("join_token", None)
                    prefix = f"round-{len(completed_rounds) + 1}-" if args.rounds > 1 else ""
                    (directory / (prefix + phase + ".json")).write_text(json.dumps(evidence, indent=2))
                    last_state = phase
                screenshot = root / "game/mod_hl2mp/screenshots/matchmaking_gameplay.jpg"
                if not copied and screenshot.exists() and screenshot.stat().st_mtime >= round_start:
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
                if "match started" in log_text and native_started is None:
                    native_started = time.monotonic()
                if completed_rounds and not copied and native_started is not None and time.monotonic() - native_started >= 6:
                    # The client's automatic preview screenshot is taken once
                    # per process. Capture later rounds from the same renderer.
                    capture = subprocess.run(["powershell.exe", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", str(root / "tools/capture-menu.ps1"), "-Name", f"dedicated-gameplay-round-{len(completed_rounds) + 1}"], cwd=root, check=True, capture_output=True, text=True)
                    print("In-game screenshot: " + capture.stdout.strip(), flush=True)
                    copied = True
                if args.unreserved and "Matchmaking_reservation_ended" in log_text and "engine authenticated player" in log_text:
                    if "admitted player" in log_text:
                        raise RuntimeError("An unreserved player was admitted")
                    proof = {"check": "unreserved_player_rejected", "success": True}
                    (directory / "verification.json").write_text(json.dumps(proof, indent=2))
                    print("Verified: an authenticated Steam player with a different coordinator identity was rejected.", flush=True)
                    verified = True
                    break
                if args.reconnect and copied and reconnect_started is None:
                    reconnect_match = match["id"] if match else None
                    if not reconnect_match:
                        raise RuntimeError("The player had no active match before disconnecting")
                    subprocess.run([str(sdk / "hl2_win64.exe"), "-hijack", "-game", str(root / "game/mod_hl2mp"), "+disconnect", "+gameui_activate"], cwd=sdk, check=True, timeout=5, creationflags=subprocess.CREATE_NO_WINDOW)
                    reconnect_started = time.monotonic()
                    print("Disconnected the native client; keeping its match reservation.", flush=True)
                if args.reconnect and reconnect_started is not None and not reconnect_requested and time.monotonic() - reconnect_started >= 5:
                    if not match or match["id"] != reconnect_match or match["state"] != "active":
                        raise RuntimeError("The match reservation disappeared before reconnecting")
                    capture = subprocess.run(["powershell.exe", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", str(root / "tools/capture-menu.ps1"), "-Name", "reconnect-menu"], cwd=root, check=True, capture_output=True, text=True)
                    print("Reconnect screenshot: " + capture.stdout.strip(), flush=True)
                    subprocess.run([str(sdk / "hl2_win64.exe"), "-hijack", "-game", str(root / "game/mod_hl2mp"), "+mm_match_connect"], cwd=sdk, check=True, timeout=5, creationflags=subprocess.CREATE_NO_WINDOW)
                    reconnect_requested = True
                if reconnect_requested and not rejoined and log_text.count("admitted player") >= 2:
                    if not match or match["id"] != reconnect_match or log_text.count("match started") != 1:
                        raise RuntimeError("Reconnecting restarted or replaced the active match")
                    rejoined = True
                    print("The native client was readmitted to the same running match.", flush=True)
                if (args.verify or args.expired_session or (args.reconnect and rejoined)) and copied and not shortened:
                    if not args.custom:
                        console_command(server, "mp_timelimit 1")
                    shortened = True
                    if args.expired_session:
                        # Rotate the test harness session. The native client's old
                        # session is revoked and cannot read its completion status.
                        rotated = client.post("/v1/auth/development", json={"steam_id": steam_id, "key": key})
                        rotated.raise_for_status()
                        headers = {"Authorization": "Bearer " + rotated.json()["token"]}
                last = state.get("last_match") or {}
                if args.coordinator_outage and copied and outage_started is None:
                    fault_proxy.unavailable.set()
                    outage_started = time.monotonic()
                    print("Interrupting native server API traffic.", flush=True)
                aborted = "coordinator contact lost; cancelling without a result" in log_text
                if args.coordinator_outage and outage_started is not None and not outage_recovered:
                    elapsed = time.monotonic() - outage_started
                    if (args.coordinator_outage == "brief" and elapsed >= 5) or aborted:
                        fault_proxy.unavailable.clear()
                        outage_recovered = True
                        print("Restored native server API traffic.", flush=True)
                        if args.coordinator_outage == "brief":
                            if aborted:
                                raise RuntimeError("A brief outage unexpectedly cancelled the match")
                            console_command(server, "mp_timelimit 1")
                    elif elapsed > 25:
                        raise RuntimeError("The server failed to cancel after sustained contact loss")
                outage_terminal = args.coordinator_outage and outage_recovered and last.get("state") in {"finished", "cancelled"}
                new_result = not match and last.get("id") not in {item["summary"]["id"] for item in completed_rounds}
                if new_result and (((args.verify or args.expired_session or args.reconnect) and last.get("state") == "finished") or outage_terminal):
                    if "admitted player" not in log_text or "match started" not in log_text:
                        raise RuntimeError("Result arrived without native admission and gameplay")
                    if args.coordinator_outage == "sustained":
                        if not aborted or last["state"] != "cancelled" or last.get("reason") != "The dedicated server could not continue":
                            raise RuntimeError("The match was not cancelled by the native outage guard")
                        if last["rating_change"] != 0 or any(r != 1500 for r in state["ratings"].values()):
                            raise RuntimeError("The cancelled match changed a rating")
                    elif last.get("result") != {"winner": None, "rating_change_team_0": 0}:
                        raise RuntimeError("Casual match unexpectedly affected ratings")
                    with sqlite3.connect(f"file:{(directory / 'state.sqlite3').as_posix()}?mode=ro", uri=True) as db:
                        stored = json.loads(db.execute("SELECT payload FROM state WHERE id=1").fetchone()[0])
                    if stored["servers"]["local-srcds"]["match"] is None:
                        if args.coordinator_outage == "sustained" and (any(path.endswith("/result") for path in fault_proxy.forwarded) or not any(path.endswith("/abort") for path in fault_proxy.forwarded)):
                            raise RuntimeError("The native server reported a result instead of aborting")
                        with client_log.open("rb") as stream:
                            stream.seek(client_offset)
                            client_text = stream.read().decode("utf-8", errors="replace")
                        returned = "returning from completed reservation" in client_text
                        kicked = "Matchmaking_reservation_ended" in log_text
                        if args.expired_session:
                            if returned or not kicked or "match ended; waiting for clients to return" not in log_text:
                                raise RuntimeError("The expired client did not exercise the return timeout")
                        else:
                            if not returned or kicked:
                                raise RuntimeError("The player did not return voluntarily after the result")
                            capture = subprocess.run(["powershell.exe", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", str(root / "tools/capture-menu.ps1"), "-Name", "match-result"], cwd=root, check=True, capture_output=True, text=True)
                            print("Result screenshot: " + capture.stdout.strip(), flush=True)
                        if args.reconnect and not rejoined:
                            raise RuntimeError("The match finished without a verified native reconnect")
                        check = "coordinator_outage_" + args.coordinator_outage if args.coordinator_outage else "expired_session_return_timeout" if args.expired_session else "native_casual_reconnect" if args.reconnect else "native_private_lifecycle" if args.custom else "native_casual_lifecycle"
                        proof = {"check": check, "success": True, "result": last.get("result"), "server_released": True, "returned_to_menu": returned, "summary": last, "vac_enabled": True}
                        if args.reconnect:
                            proof["reconnected_to_same_match"] = True
                        completed_rounds.append(proof)
                        if args.rounds > 1:
                            (directory / f"round-{len(completed_rounds)}-verification.json").write_text(json.dumps(proof, indent=2))
                            if len(completed_rounds) < args.rounds:
                                print(f"Round {len(completed_rounds)} completed; reusing the same client and dedicated server.", flush=True)
                                round_start = time.time()
                                log_offset = server_log.stat().st_size
                                client_offset = client_log.stat().st_size
                                last_state, copied, shortened, native_started = None, False, False, None
                                subprocess.run([str(sdk / "hl2_win64.exe"), "-hijack", "-game", str(root / "game/mod_hl2mp"), "+mm_queue", "casual"], cwd=sdk, check=True, timeout=5, creationflags=subprocess.CREATE_NO_WINDOW)
                                continue
                            proof = {"check": "native_consecutive_casual_rounds", "success": True,
                                     "server_pid": server.pid, "client_pid": game.pid, "rounds": completed_rounds}
                        (directory / "verification.json").write_text(json.dumps(proof, indent=2))
                        if args.coordinator_outage:
                            print(f"Verified: {args.coordinator_outage} coordinator outage handled and server released.", flush=True)
                        else:
                            print("Verified: session-loss fallback released the server." if args.expired_session else "Verified: native admission, gameplay, result reporting, and server release.", flush=True)
                        verified = True
                        break
                time.sleep(1)
            if (args.verify or args.reconnect or args.unreserved or args.expired_session or args.coordinator_outage) and not verified:
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
        if args.custom:
            custom_script.unlink(missing_ok=True)
        if fault_proxy:
            fault_proxy.close()
        for name in ("matchmaking-server.log", "matchmaking-client.log"):
            source = root / "game/mod_hl2mp" / name
            if source.exists():
                shutil.copyfile(source, directory / name)


if __name__ == "__main__":
    main()

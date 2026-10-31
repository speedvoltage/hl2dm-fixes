"""Verify a Windows Steam client playing a casual match on real Linux SRCDS in WSL.

The coordinator also runs on Linux. Its login uses explicit loopback development
credentials; SRCDS still authenticates the real Steam player. Only this helper's
children are stopped. The Linux worker can also run with an external client.
"""
import argparse
import ipaddress
import json
import os
from pathlib import Path
import secrets
import shutil
import socket
import subprocess
import sys
import threading
import time
from matchmaking_faults import ServerFaultProxy

ROOT = Path(__file__).resolve().parents[1]


def free_port(kind=socket.SOCK_STREAM, address="127.0.0.1"):
    with socket.socket(socket.AF_INET, kind) as listener:
        listener.bind((address, 0))
        return listener.getsockname()[1]


def worker(args):
    import httpx
    import pty
    import uvicorn
    sys.path.insert(0, str(ROOT))
    from services.coordinator.api import Settings, create_app
    from services.coordinator.core import Coordinator

    address = str(ipaddress.IPv4Address(args.address))
    game_port = free_port(socket.SOCK_DGRAM, address)
    url = f"http://127.0.0.1:{args.api_port}"
    key, server_key = secrets.token_urlsafe(32), secrets.token_urlsafe(32)
    core = Coordinator(args.evidence / "state.sqlite3", [dict(id="linux-srcds", token=server_key,
        address=address, port=game_port, region="eu", modes=["casual"], capacity=16)])
    app = create_app(core, Settings(development_key=key))
    service = uvicorn.Server(uvicorn.Config(app, host="127.0.0.1", port=args.api_port, log_level="warning"))
    thread = threading.Thread(target=service.run, daemon=True)
    server = None
    console = None
    reader = None
    success = False
    fault_proxy = None
    thread.start()
    try:
        for attempt in range(100):
            if service.started:
                break
            if not thread.is_alive():
                raise RuntimeError("Linux coordinator stopped before startup")
            time.sleep(0.1)
        else:
            raise RuntimeError("Linux coordinator did not start")
        if args.coordinator_outage:
            fault_proxy = ServerFaultProxy(url)
        env = dict(os.environ, LD_LIBRARY_PATH=str(args.runtime / "bin/linux64"),
                   HL2MP_MM_SERVER_URL=fault_proxy.url if fault_proxy else url, HL2MP_MM_SERVER_TOKEN=server_key)
        with httpx.Client(base_url=url, timeout=5) as client:
            console, slave = pty.openpty()
            try:
                server = subprocess.Popen([str(args.runtime / "srcds_linux64"), "-console", "-game",
                str(ROOT / "game/mod_hl2mp"), "-mm_server", "-ip", address, "-port", str(game_port),
                "+con_logfile", str(args.evidence / "native.log"), "+sv_lan", "0", "+map", "dm_lockdown"],
                cwd=args.runtime, env=env, stdin=slave, stdout=slave, stderr=slave, start_new_session=True)
            finally:
                os.close(slave)
            def capture_console():
                with (args.evidence / "server.log").open("wb") as stream:
                    while True:
                        try:
                            chunk = os.read(console, 8192)
                        except OSError:
                            break
                        if not chunk:
                            break
                        stream.write(chunk)
                        stream.flush()
            reader = threading.Thread(target=capture_console, daemon=True)
            reader.start()
            response = client.post("/v1/auth/development", json={"steam_id": args.steam_id, "key": key})
            response.raise_for_status()
            token = response.json()["token"]
            session = args.evidence / "client-session.json"
            session.write_text(json.dumps(dict(url=url, token=token, address=address, port=game_port)))
            session.chmod(0o600)
            print(f"Linux SRCDS PID {server.pid}; evidence: {args.evidence}", flush=True)
            headers = {"Authorization": "Bearer " + token}
            started, shortened, last_state = time.monotonic(), False, None
            outage_started, outage_recovered = None, not args.coordinator_outage
            while time.monotonic() - started < args.seconds and not (args.evidence / "stop").exists():
                if server.poll() is not None:
                    raise RuntimeError(f"Linux SRCDS exited with status {server.returncode}; inspect server.log")
                response = client.get("/v1/me", headers=headers)
                response.raise_for_status()
                state = response.json()
                match = state.get("match")
                phase = match["state"] if match else ("queued" if state.get("queue") else "idle")
                if phase != last_state:
                    print(f"Player state: {phase}", flush=True)
                    if match and match.get("connection"):
                        match["connection"].pop("join_token", None)
                    evidence = {k: state.get(k) for k in ("queue", "match", "last_match")}
                    (args.evidence / (phase + ".json")).write_text(json.dumps(evidence, indent=2))
                    last_state = phase
                text = (args.evidence / "server.log").read_text(errors="replace")
                if not shortened and "Matchmaking server: match started." in text:
                    if fault_proxy:
                        fault_proxy.unavailable.set()
                        outage_started = time.monotonic()
                        print("Interrupting Linux server API traffic.", flush=True)
                    else:
                        os.write(console, b"mp_timelimit 1\n")
                    shortened = True
                aborted = "coordinator contact lost; cancelling without a result" in text
                if outage_started is not None and not outage_recovered:
                    elapsed = time.monotonic() - outage_started
                    if (args.coordinator_outage == "brief" and elapsed >= 5) or aborted:
                        fault_proxy.unavailable.clear()
                        outage_recovered = True
                        print("Restored Linux server API traffic.", flush=True)
                        if args.coordinator_outage == "brief":
                            if aborted:
                                raise RuntimeError("A brief outage unexpectedly cancelled the match")
                            os.write(console, b"mp_timelimit 1\n")
                    elif elapsed > 25:
                        raise RuntimeError("Linux server failed to cancel after sustained contact loss")
                last = state.get("last_match") or {}
                if last.get("state") in {"finished", "cancelled"} and shortened and outage_recovered and "Matchmaking server: admitted player" in text:
                    if args.coordinator_outage == "sustained":
                        if not aborted or last["state"] != "cancelled" or last.get("reason") != "The dedicated server could not continue":
                            raise RuntimeError("The Linux native outage guard did not cancel the match")
                        if last["rating_change"] != 0 or state["ratings"] != {"duel": 1500, "team": 1500}:
                            raise RuntimeError("The cancelled Linux match changed a rating")
                    elif last.get("result") != {"winner": None, "rating_change_team_0": 0}:
                        raise RuntimeError("Linux match did not finish with an unrated casual result")
                    with core.transaction() as data:
                        released = data["servers"]["linux-srcds"]["match"] is None
                    if released:
                        if args.coordinator_outage == "sustained" and (any(path.endswith("/result") for path in fault_proxy.forwarded) or not any(path.endswith("/abort") for path in fault_proxy.forwarded)):
                            raise RuntimeError("The Linux server reported a result instead of aborting")
                        if "Matchmaking_reservation_ended" in text:
                            raise RuntimeError("The server kicked the player instead of allowing a clean return")
                        success = True
                        (args.evidence / "verification.json").write_text(json.dumps(dict(
                            check="linux_coordinator_outage_" + args.coordinator_outage if args.coordinator_outage else "linux_server_casual_lifecycle", success=True,
                            result=last.get("result"), server_released=True, summary=last), indent=2))
                        print("Verified Linux match resolution and server release.", flush=True)
                        break
                time.sleep(0.5)
            if not success:
                raise RuntimeError("Linux lifecycle check did not complete; inspect the evidence directory")
    finally:
        if server and server.poll() is None:
            try:
                os.write(console, b"quit\n")
                server.wait(timeout=5)
            except (OSError, subprocess.TimeoutExpired):
                server.terminate()
                try:
                    server.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    server.kill()
                    server.wait()
        if reader:
            reader.join(timeout=3)
        if console is not None:
            os.close(console)
        if success and server:
            # Match completion and process shutdown are separate outcomes. Some
            # SDK runtime builds assert on quit even without the matchmaking agent.
            verification = args.evidence / "verification.json"
            result = json.loads(verification.read_text())
            console_text = (args.evidence / "server.log").read_text(errors="replace")
            result["server_exit_code"] = server.returncode
            result["shutdown_clean"] = server.returncode == 0 and "Assertion Failed" not in console_text
            verification.write_text(json.dumps(result, indent=2))
        if fault_proxy:
            fault_proxy.close()
        service.should_exit = True
        thread.join(timeout=10)
        if not thread.is_alive():
            core.close()


def windows(args):
    import winreg
    with winreg.OpenKey(winreg.HKEY_CURRENT_USER, r"Software\Valve\Steam\ActiveProcess") as key:
        account = winreg.QueryValueEx(key, "ActiveUser")[0]
    if not account:
        raise RuntimeError("Sign into Steam before running this check")
    evidence = ROOT / "artifacts" / ("linux-dedicated-preview-" + time.strftime("%Y%m%d-%H%M%S"))
    evidence.mkdir()
    wsl = ["wsl.exe", "-d", args.distro, "--exec"]
    def linux_path(path):
        return subprocess.check_output(wsl + ["wslpath", "-a", "-u", path.resolve().as_posix()], text=True).strip()
    address = subprocess.check_output(wsl + ["hostname", "-I"], text=True).split()[0]
    api_port = free_port()
    command = wsl + [linux_path(ROOT / "artifacts/coordinator-venv-linux/bin/python"),
        linux_path(Path(__file__)), "--worker", "--runtime", linux_path(args.runtime),
        "--evidence", linux_path(evidence), "--steam-id", str(76561197960265728 + account),
        "--address", address, "--api-port", str(api_port), "--seconds", str(args.seconds)]
    if args.coordinator_outage:
        command += ["--coordinator-outage", args.coordinator_outage]
    worker_log = (evidence / "worker.log").open("w")
    child = subprocess.Popen(command, stdout=worker_log, stderr=subprocess.STDOUT,
                             creationflags=subprocess.CREATE_NO_WINDOW)
    game = None
    try:
        session_path = evidence / "client-session.json"
        for attempt in range(200):
            if session_path.exists():
                try:
                    session = json.loads(session_path.read_text())
                    break
                except json.JSONDecodeError:
                    pass
            if child.poll() is not None:
                raise RuntimeError("Linux worker stopped during setup")
            time.sleep(0.1)
        else:
            raise RuntimeError("Linux worker did not publish a session")
        sdk = Path(os.environ.get("ProgramFiles(x86)", "C:/Program Files (x86)")) / "Steam/steamapps/common/Source SDK Base 2013 Multiplayer"
        env = dict(os.environ, HL2MP_MM_DEV_URL=session["url"], HL2MP_MM_DEV_SESSION=session["token"])
        client_log = ROOT / "game/mod_hl2mp/matchmaking-linux-client.log"
        client_offset = client_log.stat().st_size if client_log.exists() else 0
        started = time.time()
        game = subprocess.Popen([str(sdk / "hl2_win64.exe"), "-steam", "-game", str(ROOT / "game/mod_hl2mp"),
            "-windowed", "-w", "1280", "-h", "720", "-novid", "-mm_local_test", "+con_logfile", "matchmaking-linux-client.log", "+clientport",
            str(free_port(socket.SOCK_DGRAM)), "+mm_queue", "casual"], cwd=sdk, env=env)
        print(f"Windows client PID {game.pid}; Linux server {session['address']}:{session['port']}", flush=True)
        captured = False
        while child.poll() is None:
            if game.poll() is not None:
                raise RuntimeError("The game client closed before verification finished")
            screenshot = ROOT / "game/mod_hl2mp/screenshots/matchmaking_gameplay.jpg"
            if not captured and screenshot.exists() and screenshot.stat().st_mtime >= started:
                target = ROOT / "artifacts/screenshots" / (time.strftime("%Y%m%d-%H%M%S") + "-linux-server-gameplay.jpg")
                shutil.copyfile(screenshot, target)
                print(f"In-game screenshot: {target}", flush=True)
                captured = True
            time.sleep(0.5)
        if child.returncode:
            raise RuntimeError(f"Linux worker failed; inspect {evidence}")
        if not captured:
            raise RuntimeError("The match completed without a new gameplay screenshot")
        print(f"Linux lifecycle verified. Evidence: {evidence}", flush=True)
        result = json.loads((evidence / "verification.json").read_text())
        with client_log.open("rb") as stream:
            stream.seek(client_offset)
            client_text = stream.read().decode("utf-8", errors="replace")
        (evidence / "client.log").write_text(client_text)
        if "returning from completed reservation" not in client_text:
            raise RuntimeError("The client did not return voluntarily after the Linux match")
        capture = subprocess.run(["powershell.exe", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", str(ROOT / "tools/capture-menu.ps1"), "-Name", "linux-match-result"], cwd=ROOT, check=True, capture_output=True, text=True)
        print("Result screenshot: " + capture.stdout.strip(), flush=True)
        result["returned_to_menu"] = True
        (evidence / "verification.json").write_text(json.dumps(result, indent=2))
        if not result.get("shutdown_clean"):
            print("The SDK runtime reported shutdown diagnostics; see verification.json and server.log.", flush=True)
    finally:
        (evidence / "stop").touch()
        if game and game.poll() is None:
            game.terminate()
            game.wait(timeout=10)
        # Let the worker stop its server and coordinator, rather than orphaning them.
        child.wait(timeout=25)
        worker_log.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runtime", type=Path, required=True)
    parser.add_argument("--distro", default="Ubuntu")
    parser.add_argument("--seconds", type=int, default=180)
    parser.add_argument("--coordinator-outage", choices=("brief", "sustained"))
    parser.add_argument("--worker", action="store_true", help=argparse.SUPPRESS)
    parser.add_argument("--evidence", type=Path, help=argparse.SUPPRESS)
    parser.add_argument("--steam-id", help=argparse.SUPPRESS)
    parser.add_argument("--address", help=argparse.SUPPRESS)
    parser.add_argument("--api-port", type=int, help=argparse.SUPPRESS)
    args = parser.parse_args()
    if not 90 <= args.seconds <= 600:
        parser.error("Choose a duration between 90 and 600 seconds")
    if args.worker:
        if os.name != "posix" or not all((args.evidence, args.steam_id, args.address, args.api_port)):
            parser.error("The worker requires Linux and its explicit connection parameters")
        worker(args)
    elif os.name == "nt":
        windows(args)
    else:
        parser.error("Launch this WSL check from Windows, or use the worker with an external client")


if __name__ == "__main__":
    main()

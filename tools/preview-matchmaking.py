"""Run a local UI exercise with a simulated opponent and server registration.

This does not start SRCDS or attest that an actual game server is ready. It stops
at the server-starting phase after the player accepts. All credentials are local,
temporary development credentials, and the API binds only to loopback.
"""
import argparse
import json
import os
from pathlib import Path
import secrets
import shutil
import socket
import subprocess
import sys
import time

import httpx


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--width", type=int, default=1280)
    parser.add_argument("--height", type=int, default=720)
    parser.add_argument("--seconds", type=int, default=0, help="Close the preview after this many seconds; 0 waits for the game to close.")
    args = parser.parse_args()
    if args.width < 640 or args.height < 480 or args.seconds < 0:
        parser.error("Use at least 640x480 and a nonnegative duration.")
    if os.name != "nt":
        raise SystemExit("This preview launcher currently targets the Windows game client.")
    root = Path(__file__).resolve().parents[1]
    directory = root / "artifacts" / ("coordinator-preview-" + time.strftime("%Y%m%d-%H%M%S"))
    directory.mkdir(parents=True)
    with socket.socket() as listener:
        listener.bind(("127.0.0.1", 0))
        port = listener.getsockname()[1]
    url = f"http://127.0.0.1:{port}"
    key, server_key = secrets.token_urlsafe(32), secrets.token_urlsafe(32)
    config = directory / "servers.json"
    config.write_text(json.dumps([dict(id="preview", token=server_key, address="127.0.0.1", port=27015, region="eu", modes=["duel"])]))
    env = dict(os.environ, MM_DEVELOPMENT_KEY=key)
    log = (directory / "coordinator.log").open("w")
    service = subprocess.Popen([sys.executable, "-m", "services.coordinator", "--development", "--port", str(port), "--servers", str(config), "--database", str(directory / "state.sqlite3")], cwd=root, env=env, stdout=log, stderr=subprocess.STDOUT, creationflags=subprocess.CREATE_NO_WINDOW)
    game = None
    try:
        with httpx.Client(base_url=url, timeout=5) as client:
            for attempt in range(50):
                if service.poll() is not None:
                    raise RuntimeError("Coordinator exited; inspect its local log")
                try:
                    if client.get("/v1/health").status_code == 200:
                        break
                except httpx.TransportError:
                    pass
                time.sleep(0.2)
            else:
                raise RuntimeError("Coordinator did not become ready")
            tokens = []
            for uid in ("76561198000000001", "76561198000000002"):
                response = client.post("/v1/auth/development", json={"steam_id": uid, "key": key})
                response.raise_for_status()
                tokens.append(response.json()["token"])
            server = {"Authorization": "Bearer " + server_key}
            opponent = {"Authorization": "Bearer " + tokens[1]}
            player = {"Authorization": "Bearer " + tokens[0]}
            client.post("/v1/servers/heartbeat", json={}, headers=server).raise_for_status()
            client.post("/v1/queue", json={"mode": "duel", "region": "eu", "protocol": 1}, headers=opponent).raise_for_status()
            sdk = Path(os.environ.get("ProgramFiles(x86)", "C:/Program Files (x86)")) / "Steam/steamapps/common/Source SDK Base 2013 Multiplayer"
            game_env = dict(os.environ, HL2MP_MM_DEV_SESSION=tokens[0], HL2MP_MM_DEV_URL=url)
            game = subprocess.Popen([str(sdk / "hl2_win64.exe"), "-steam", "-game", str(root / "game/mod_hl2mp"), "-windowed", "-w", str(args.width), "-h", str(args.height), "-novid", "-condebug", "-mm_local_test", "+mm_queue", "duel"], cwd=sdk, env=game_env)
            (directory / "processes.json").write_text(json.dumps({"service_pid": service.pid, "game_pid": game.pid, "url": url}))
            print(f"Local development preview: {url}; game PID {game.pid}", flush=True)
            print(f"Evidence directory: {directory}", flush=True)
            last_state = None
            started = time.time()
            captured = False
            while game.poll() is None and service.poll() is None:
                if args.seconds and time.time() - started >= args.seconds:
                    break
                client.post("/v1/servers/heartbeat", json={}, headers=server).raise_for_status()
                response = client.get("/v1/me", headers=opponent)
                response.raise_for_status()
                rival = response.json()
                match = rival["match"]
                if match and match["state"] == "ready_check" and not match["you_accepted"]:
                    client.post(f'/v1/matches/{match["id"]}/confirm', json={"accept": True}, headers=opponent).raise_for_status()
                current = client.get("/v1/me", headers=player).json()
                current_state = current.get("match", {}).get("state") if current.get("match") else ("queued" if current.get("queue") else "idle")
                if current_state != last_state:
                    print("Player state: " + current_state, flush=True)
                    # Save no bearer tokens or join grants in the evidence snapshot.
                    evidence = {k: current.get(k) for k in ("protocol", "queue", "match", "last_match")}
                    match_evidence = evidence.get("match")
                    if match_evidence and match_evidence.get("connection"):
                        match_evidence["connection"].pop("join_token", None)
                    (directory / (current_state + ".json")).write_text(json.dumps(evidence, indent=2))
                    last_state = current_state
                screenshot = root / "game/mod_hl2mp/screenshots/matchmaking_ready_check.jpg"
                if not captured and screenshot.exists() and screenshot.stat().st_mtime >= started:
                    output = root / "artifacts/screenshots" / (time.strftime("%Y%m%d-%H%M%S") + f"-matchmaking-ready-check-{args.width}x{args.height}.jpg")
                    output.parent.mkdir(parents=True, exist_ok=True)
                    shutil.copyfile(screenshot, output)
                    print(f"In-game screenshot: {output}", flush=True)
                    captured = True
                time.sleep(1)
            if args.seconds and not captured:
                raise RuntimeError("The preview ended without a fresh ready-check screenshot.")
    finally:
        if game is not None and game.poll() is None:
            game.terminate()
            try:
                game.wait(timeout=10)
            except subprocess.TimeoutExpired:
                game.kill()
                game.wait()
        service.terminate()
        try:
            service.wait(timeout=10)
        except subprocess.TimeoutExpired:
            service.kill()
            service.wait()
        log.close()


if __name__ == "__main__":
    main()

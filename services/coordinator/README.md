# Matchmaking coordinator

This is the initial self-hosted coordinator for a small dedicated-server pool.
The service supports duels, provisional 2v2 team deathmatch, and casual sessions.
Parties stay on the same team. Ranked searches widen their rating tolerance with
time; casual sessions accept backfill and do not change ratings.

## Run and test locally

From the repository root on Windows:

```powershell
python -m venv artifacts/coordinator-venv
.\artifacts\coordinator-venv\Scripts\python.exe -m pip install -r services/coordinator/requirements.txt
.\artifacts\coordinator-venv\Scripts\python.exe -m unittest discover -s services/coordinator/tests -v
```

On Linux, `bash tools/test-coordinator.sh` creates a virtual environment and runs
the same suite. Tests cover party integrity and consent, competing allocators,
ready-check expiry, server loss, authorization boundaries, casual backfill,
session replay/revocation, and idempotent rating updates. Steam is mocked in
these tests; they do not establish live Steamworks compatibility.

After building the Windows mod, run:

```powershell
.\artifacts\coordinator-venv\Scripts\python.exe tools/preview-matchmaking.py
```

The preview starts a loopback coordinator, two simulated accounts, a simulated
server registration, and the real game client. It queues the opponent and
accepts the opponent's ready check. Accept or decline your match in the menu.
The ready check expires after 30 seconds. Accepting reaches the server-starting
phase; this helper does not launch SRCDS or claim that a game server is ready.
Closing the game stops the helper's coordinator.

Evidence is written to `artifacts/coordinator-preview-<timestamp>/`; an actual
renderer capture is copied to `artifacts/screenshots/`. The temporary server
configuration contains a local credential and stays in ignored artifacts.
Development sessions and URLs are passed through the child process environment,
enabled only by `-mm_local_test`, and are not archived in the game's config.

## Configured service

Copy `servers.example.json` to a private file and replace every placeholder
with a separately generated 32+ character credential. A server entry declares
its ID, IPv4 address, port, region, capacity, and supported modes. The example
cannot run unchanged. Server credentials authorize reservations and results;
player bearer tokens cannot call server endpoints.

```text
python -m services.coordinator --servers <private-servers.json> --database <state.sqlite3>
```

The default bind is `127.0.0.1:8787`. Set `MM_STEAM_APP_ID` to the project's own
app ID and `MM_STEAM_PUBLISHER_KEY` to a server-side Steamworks publisher key.
The native client requests a Web API ticket with identity `hl2dm-matchmaking`;
the service checks it with Steam's AuthenticateUserTicket endpoint before
issuing a one-hour session. A new login revokes the user's previous session.
Publisher/VAC-banned accounts are rejected. The publisher key never goes into
the mod, lobby metadata, or a player request.

For an Internet deployment, expose the service through HTTPS and configure
`mm_coordinator_url` in the client. HTTPS certificate verification is required.
Only `http://127.0.0.1:<port>` is allowed for the local preview. Development mode
requires `--development` plus a 32+ character `MM_DEVELOPMENT_KEY`, and must stay
on loopback without a proxy. Production mode leaves that login disabled.

The current rate limiter uses the direct peer address and ignores forwarding
headers: 20 authentication requests/minute and 300 other requests/minute.
A reverse proxy therefore shares one limit across its clients in this initial
version; trusted proxy handling is still needed for a larger deployment.
Request bodies are limited to 16 KiB. Keep HTTP debug logging off: Steam's
upstream authentication URL contains the publisher key and ticket.

## Lifecycle and trust boundaries

| Step | Player API | Dedicated-server API |
| --- | --- | --- |
| Identity | `POST /v1/auth/steam`; bearer session thereafter | Separate configured bearer credential |
| Party | `GET /v1/me`, `POST /v1/party/join`, `/leave`, `/ready` | No authority over player parties |
| Search | `POST /v1/queue`, `DELETE /v1/queue` | `POST /v1/servers/heartbeat` |
| Ready check | `POST /v1/matches/{id}/confirm` | Reservation with roster, mode, and map |
| Server loaded | Wait for active assignment | `POST /v1/servers/matches/{id}/ready` |
| Admission | Receive endpoint and private join grant | `POST /v1/servers/matches/{id}/authorize` |
| Completion | Read `last_match` and ratings | `POST /v1/servers/matches/{id}/result` |
| Casual exit | `POST /v1/casual/leave` | Receive updated roster on heartbeat |

The game server must validate the connecting player's Steam identity itself and
pair it with that player's join grant. The service binds grants to the player,
match, and authenticated server. Lobby data and client-reported scores cannot
authorize a connection or rating update. The native game-server integration
that performs this enforcement is still pending.

Players poll `/v1/me` to maintain presence. Queued parties expire after 45 seconds
without member presence; ready checks last 30 seconds; server startup has a
120-second deadline. Servers heartbeat within 30 seconds. Losing a server
cancels the match without changing ratings. A cancelled/finished server remains
reserved until it acknowledges stopping that match with `running_match: null`.
Results commit once; a repeated identical result is safe, while a conflicting
duplicate is rejected. Terminal records are kept for seven days, longer when
the server still claims the match.

SQLite WAL stores one serialized state snapshot. `BEGIN IMMEDIATE` serializes
allocation and result mutations across processes, and ratings commit in the
same transaction as match completion. This is suited to an initial small pool;
there is no throughput claim. Use a local disk and back up with SQLite's backup
API, not by copying an active database without its WAL. A normalized storage
schema, administrative server removal, and operational tooling remain future
work.

Steam reference: [AuthenticateUserTicket](https://partner.steamgames.com/doc/webapi/ISteamUserAuth).

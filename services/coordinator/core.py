"""Transactional matchmaking. Transport and Steam authentication live in api.py.

The initial deployment uses one SQLite snapshot per transaction. BEGIN IMMEDIATE
serializes writers even across processes; mutations and ratings commit together.
This favors auditable invariants over throughput for the initial server pool.
"""
from __future__ import annotations

from contextlib import contextmanager
from hashlib import sha256
from itertools import combinations
import ipaddress
import json
import math
import secrets
import sqlite3
import threading
import time


MODES = {
    "duel": {"team_size": 1, "map": "dm_lockdown", "ranked": True},
    "team": {"team_size": 2, "map": "dm_lockdown", "ranked": True},
    "team3": {"team_size": 3, "map": "dm_lockdown", "ranked": True},
    "team4": {"team_size": 4, "map": "dm_overwatch", "ranked": True},
    "team5": {"team_size": 5, "map": "dm_overwatch", "ranked": True},
    "casual": {"team_size": 0, "map": "dm_lockdown", "ranked": False},
    "custom": {"team_size": 0, "map": "dm_lockdown", "ranked": False},
}
CUSTOM_MAPS = {"dm_lockdown", "dm_overwatch", "dm_powerhouse", "dm_resistance", "dm_runoff", "dm_steamlab", "dm_underpass"}
PROTOCOL = 1
MAX_PARTY_MEMBERS = 10
PRESENCE_SECONDS = 45
SERVER_SECONDS = 30
READY_SECONDS = 30
START_SECONDS = 120
MATCH_SECONDS = 7200
TERMINAL = {"finished", "cancelled"}


class Rejected(Exception):
    def __init__(self, message, status=409):
        self.message, self.status = message, status
        super().__init__(message)


def digest(value):
    return sha256(value.encode("utf-8")).hexdigest()


def identifier():
    return secrets.token_hex(16)


class Coordinator:
    def __init__(self, database, servers=None, clock=time.time):
        self.clock = clock
        self.lock = threading.RLock()
        self.db = sqlite3.connect(str(database), check_same_thread=False, timeout=15)
        self.db.execute("PRAGMA journal_mode=WAL")
        self.db.execute("PRAGMA synchronous=FULL")
        self.db.execute("CREATE TABLE IF NOT EXISTS state (id INTEGER PRIMARY KEY CHECK(id=1), payload TEXT NOT NULL)")
        initial = dict(version=1, users={}, sessions={}, parties={}, queue={}, servers={}, matches={}, tickets={})
        self.db.execute("INSERT OR IGNORE INTO state VALUES (1,?)", (json.dumps(initial),))
        self.db.commit()
        try:
            with self.transaction() as state:
                if state["version"] != 1:
                    raise RuntimeError("Unsupported coordinator database version")
                for user in state["users"].values():
                    for mode, spec in MODES.items():
                        if spec["ranked"]:
                            user["ratings"].setdefault(mode, 1500)
                # Omitting configuration opens the existing pool unchanged (for
                # another worker). An explicit list is the complete current pool,
                # including an empty list used to revoke every server.
                if servers is not None:
                    self._configure_pool(state, servers)
        except BaseException:
            self.db.close()
            raise

    def close(self):
        with self.lock:
            self.db.close()

    @contextmanager
    def transaction(self):
        with self.lock:
            self.db.execute("BEGIN IMMEDIATE")
            try:
                state = json.loads(self.db.execute("SELECT payload FROM state WHERE id=1").fetchone()[0])
                yield state
                self.db.execute("UPDATE state SET payload=? WHERE id=1", (json.dumps(state, separators=(",", ":")),))
                self.db.commit()
            except BaseException:
                self.db.rollback()
                raise

    def _configure_pool(self, state, servers):
        servers = list(servers)
        identifiers = [spec.get("id") for spec in servers]
        if any(not isinstance(sid, str) or not sid for sid in identifiers) or len(set(identifiers)) != len(identifiers):
            raise ValueError("Each configured server needs a nonempty, distinct ID")
        removed = set(state["servers"]) - set(identifiers)
        for match in state["matches"].values():
            if match["server"] in removed and match["state"] not in TERMINAL:
                self._cancel(state, match, "The dedicated server was removed from the pool")
        for sid in removed:
            del state["servers"][sid]
        for spec in servers:
            self._configure_server(state, spec)

    def _configure_server(self, state, spec):
        required = {"id", "token", "address", "port", "region", "modes"}
        if not required <= spec.keys() or len(spec["token"]) < 32 or spec["token"].startswith("REPLACE_"):
            raise ValueError("Server configuration requires an ID, a 32+ character token, endpoint, region, and modes")
        ipaddress.IPv4Address(spec["address"])
        if not 1 <= spec["port"] <= 65535 or not spec["modes"] or not set(spec["modes"]) <= MODES.keys():
            raise ValueError("Invalid server port or modes")
        capacity = spec.get("capacity", 16)
        if not 4 <= capacity <= 64:
            raise ValueError("Server capacity must be between 4 and 64")
        for other in state["servers"].values():
            if other["id"] != spec["id"] and other["token_hash"] == digest(spec["token"]):
                raise ValueError("Each server needs a distinct credential")
        old = state["servers"].get(spec["id"], {})
        state["servers"][spec["id"]] = {
            **{k: spec[k] for k in required - {"token"}},
            "token_hash": digest(spec["token"]), "seen": old.get("seen", 0),
            "match": old.get("match"),
            "capacity": capacity,
            "maps": list(spec.get("maps", sorted(CUSTOM_MAPS))),
        }

    def login(self, steam_id, ticket_fingerprint=None):
        """Only call after an authentication provider has verified the identity."""
        if not steam_id.isascii() or not steam_id.isdigit() or not 0 < int(steam_id) < 2**64:
            raise Rejected("Invalid Steam identity", 401)
        now, token = self.clock(), secrets.token_urlsafe(32)
        with self.transaction() as s:
            if ticket_fingerprint and ticket_fingerprint in s["tickets"]:
                raise Rejected("Ticket already exchanged; request a fresh Steam ticket", 401)
            user = s["users"].setdefault(steam_id, {"party": None, "match": None, "seen": now, "ratings": {mode: 1500 for mode, spec in MODES.items() if spec["ranked"]}})
            user["seen"] = now
            # A new login revokes older sessions, without changing party/match membership.
            s["sessions"] = {k: v for k, v in s["sessions"].items() if v["user"] != steam_id and v["expires"] > now}
            s["sessions"][digest(token)] = {"user": steam_id, "expires": now + 3600}
            if ticket_fingerprint:
                s["tickets"][ticket_fingerprint] = now + 86400
            self._party(s, steam_id)
        return {"token": token, "steam_id": steam_id, "expires_in": 3600, "protocol": PROTOCOL}

    def authenticate(self, token):
        with self.transaction() as s:
            session = s["sessions"].get(digest(token))
            if not session or session["expires"] <= self.clock():
                raise Rejected("Session expired; sign in again", 401)
            return session["user"]

    def authenticate_server(self, token):
        hashed = digest(token)
        with self.transaction() as s:
            for server in s["servers"].values():
                if secrets.compare_digest(hashed, server["token_hash"]):
                    return server["id"]
        raise Rejected("Invalid server credential", 401)

    def _party(self, s, user_id):
        user = s["users"][user_id]
        if user["party"] is None:
            pid = identifier()
            s["parties"][pid] = {"id": pid, "leader": user_id, "members": [user_id], "ready": [], "invite": secrets.token_urlsafe(24), "revision": 0}
            user["party"] = pid
        return s["parties"][user["party"]]

    def _unlocked(self, s, party):
        if party["id"] in s["queue"] or any(s["users"][u]["match"] for u in party["members"]):
            raise Rejected("Leave the queue or finish the match before changing your party")

    def party_join(self, actor, invitation):
        with self.transaction() as s:
            target = next((p for p in s["parties"].values() if secrets.compare_digest(p["invite"], invitation)), None)
            if not target:
                raise Rejected("Party invitation expired", 404)
            if target.get("lobby") and actor not in target["lobby_members"]:
                raise Rejected("The party leader has not invited this account", 403)
            current = self._party(s, actor)
            if current["id"] == target["id"]:
                return
            self._unlocked(s, target)
            self._unlocked(s, current)
            if len(target["members"]) >= MAX_PARTY_MEMBERS:
                raise Rejected("Party is full")
            self._leave(s, actor)
            target["members"].append(actor)
            target["ready"] = []
            target["revision"] = target.get("revision", 0) + 1
            s["users"][actor]["party"] = target["id"]

    def _party_snapshot(self, party, party_id, revision):
        if party["id"] != party_id or party.get("revision", 0) != revision:
            raise Rejected("Your party changed. Review the roster and try again")

    def party_lobby(self, actor, lobby_id, members, party_id, revision):
        """A leader authorizes accounts, not identities claimed by lobby metadata.

        Each account still authenticates and accepts the invitation separately.
        The lobby ID is a grouping hint, never proof of Steam membership.
        """
        if (not 1 <= len(lobby_id) <= 20 or not lobby_id.isascii() or not lobby_id.isdigit() or not 0 < int(lobby_id) < 2**64
                or not 1 <= len(members) <= MAX_PARTY_MEMBERS or len(set(members)) != len(members) or actor not in members
                or any(not 1 <= len(u) <= 20 or not u.isascii() or not u.isdigit() or not 0 < int(u) < 2**64 for u in members)):
            raise Rejected("Invalid party roster", 400)
        with self.transaction() as s:
            party = self._party(s, actor)
            if party["leader"] != actor:
                raise Rejected("Only the party leader may invite or remove members", 403)
            self._party_snapshot(party, party_id, revision)
            self._unlocked(s, party)
            if party.get("lobby") != lobby_id and len(party["members"]) > 1:
                raise Rejected("Leave your previous party before creating a new lobby party")
            if party.get("lobby") == lobby_id and set(party["lobby_members"]) == set(members):
                return
            for uid in list(party["members"]):
                if uid not in members:
                    self._leave(s, uid)
                    self._party(s, uid)
            party.update(lobby=lobby_id, lobby_members=members, ready=[], invite=secrets.token_urlsafe(24))
            party["revision"] = party.get("revision", 0) + 1

    def _leave(self, s, actor):
        party = self._party(s, actor)
        party["members"].remove(actor)
        party["ready"] = []
        party["revision"] = party.get("revision", 0) + 1
        if party.get("lobby"):
            party["lobby_members"] = [u for u in party["lobby_members"] if u != actor]
        if not party["members"]:
            del s["parties"][party["id"]]
        else:
            if party["leader"] == actor:
                party["leader"] = party["members"][0]
            party["invite"] = secrets.token_urlsafe(24)
        s["users"][actor]["party"] = None

    def party_leave(self, actor):
        with self.transaction() as s:
            self._unlocked(s, self._party(s, actor))
            self._leave(s, actor)
            self._party(s, actor)

    def party_ready(self, actor, ready, party_id=None, revision=None):
        with self.transaction() as s:
            party = self._party(s, actor)
            if party.get("lobby") or party_id is not None:
                self._party_snapshot(party, party_id, revision)
            self._unlocked(s, party)
            party["ready"] = [u for u in party["ready"] if u != actor]
            if ready:
                party["ready"].append(actor)

    def enqueue(self, actor, mode, region, protocol, party_id=None, revision=None):
        if mode not in MODES or mode == "custom" or protocol != PROTOCOL:
            raise Rejected("Incompatible mode or client version", 400)
        with self.transaction() as s:
            self._expire(s)
            p = self._party(s, actor)
            if p.get("lobby") or party_id is not None:
                self._party_snapshot(p, party_id, revision)
            if p.get("lobby") and set(p["members"]) != set(p["lobby_members"]):
                raise Rejected("Waiting for every lobby member to join matchmaking")
            if p["leader"] != actor:
                raise Rejected("Only the party leader can start matchmaking", 403)
            if any(s["users"][u]["match"] for u in p["members"]):
                raise Rejected("A party member is already assigned to a match")
            existing = s["queue"].get(p["id"])
            if existing:
                if existing["mode"] == mode and existing["region"] == region:
                    return
                raise Rejected("Cancel your current search before changing queues")
            limit = MODES[mode]["team_size"] or MAX_PARTY_MEMBERS
            if len(p["members"]) > limit:
                raise Rejected(f"This mode allows parties of at most {limit}")
            if len(p["members"]) > 1 and set(p["members"]) != set(p["ready"]):
                raise Rejected("Every party member must be ready")
            if any(self.clock() - s["users"][u]["seen"] > PRESENCE_SECONDS for u in p["members"]):
                raise Rejected("A party member is offline")
            required_slots = MODES[mode]["team_size"] * 2 or len(p["members"])
            if not any(server["region"] == region and mode in server["modes"] and server["capacity"] >= required_slots for server in s["servers"].values()):
                raise Rejected("No server pool is configured for this region and mode", 503)
            s["queue"][p["id"]] = {"mode": mode, "region": region, "created": self.clock()}

    def custom_match(self, actor, region, map_name, team_size, time_limit, frag_limit, protocol, party_id=None, revision=None):
        if (protocol != PROTOCOL or map_name not in CUSTOM_MAPS or type(team_size) is not int or not 0 <= team_size <= 5
                or type(time_limit) is not int or not 1 <= time_limit <= 60
                or type(frag_limit) is not int or not 0 <= frag_limit <= 500):
            raise Rejected("Invalid private match settings", 400)
        with self.transaction() as s:
            self._expire(s)
            p = self._party(s, actor)
            if p.get("lobby") or party_id is not None:
                self._party_snapshot(p, party_id, revision)
            if p["leader"] != actor:
                raise Rejected("Only the party leader can start a private match", 403)
            self._unlocked(s, p)
            if p.get("lobby") and set(p["members"]) != set(p["lobby_members"]):
                raise Rejected("Waiting for every lobby member to join matchmaking")
            count = len(p["members"])
            if team_size and not 2 <= count <= team_size * 2:
                raise Rejected("Invite between two players and the selected match capacity")
            if count > 1 and set(p["members"]) != set(p["ready"]):
                raise Rejected("Every party member must be ready")
            if any(self.clock() - s["users"][u]["seen"] > PRESENCE_SECONDS for u in p["members"]):
                raise Rejected("A party member is offline")
            server = next((v for v in s["servers"].values() if "custom" in v["modes"] and v["region"] == region
                           and map_name in v["maps"] and v["capacity"] >= (team_size * 2 or count)
                           and not v["match"] and self.clock() - v["seen"] <= SERVER_SECONDS), None)
            if not server:
                raise Rejected("No private server is available for these settings", 503)
            mid = identifier()
            # A private party fills both sides; alternate members for balanced teams.
            players = {u: {"team": i % 2 if team_size else 0, "accepted": False, "join_token": secrets.token_urlsafe(32)}
                       for i, u in enumerate(p["members"])}
            s["matches"][mid] = {"id": mid, "server": server["id"], "mode": "custom", "map": map_name,
                "state": "ready_check", "deadline": self.clock() + READY_SECONDS, "players": players,
                "rules": {"team_size": team_size, "time_limit": time_limit, "frag_limit": frag_limit}, "reason": None, "result": None}
            server["match"] = mid
            p["ready"] = []
            for uid in players:
                s["users"][uid]["match"] = mid

    def cancel_queue(self, actor):
        with self.transaction() as s:
            # Any member may withdraw consent for the entire party.
            s["queue"].pop(self._party(s, actor)["id"], None)
            mid = s["users"][actor]["match"]
            if mid:
                # Allocation can race a DELETE sent while the client showed a queue.
                match = s["matches"][mid]
                if match["state"] == "ready_check":
                    self._cancel(s, match, "A player cancelled the search")
                elif match["state"] not in TERMINAL:
                    raise Rejected("The search has ended and the match is starting")

    def _match_summary(self, s, match, actor, state):
        ranked = MODES[match["mode"]]["ranked"]
        return {"id": match["id"], "state": state, "mode": match["mode"], "map": match["map"],
                "rating_change": 0, "rating": s["users"][actor]["ratings"][match["mode"]] if ranked else None}

    def _cancel(self, s, match, reason):
        match["state"], match["reason"] = "cancelled", reason
        match["ended"] = self.clock()
        for uid in match["players"]:
            if s["users"][uid]["match"] == match["id"]:
                s["users"][uid]["match"] = None
                s["users"][uid]["last_match"] = {**self._match_summary(s, match, uid, "cancelled"), "reason": reason}
        # Retain the server claim until it acknowledges that it has stopped the match.

    def _expire(self, s):
        now = self.clock()
        s["sessions"] = {k: v for k, v in s["sessions"].items() if v["expires"] > now}
        s["tickets"] = {k: v for k, v in s["tickets"].items() if v > now}
        for pid in list(s["queue"]):
            if any(now - s["users"][u]["seen"] > PRESENCE_SECONDS for u in s["parties"][pid]["members"]):
                del s["queue"][pid]
        for match in s["matches"].values():
            if match["state"] in TERMINAL:
                continue
            server = s["servers"][match["server"]]
            if now - server["seen"] > SERVER_SECONDS:
                self._cancel(s, match, "Server connection lost")
            elif now >= match["deadline"]:
                self._cancel(s, match, "Match confirmation or server startup timed out" if match["state"] != "active" else "Match exceeded its time limit")
        # Retain completed results for idempotent retries; never prune a server claim.
        claimed = {server["match"] for server in s["servers"].values()}
        s["matches"] = {mid: m for mid, m in s["matches"].items() if m["state"] not in TERMINAL or mid in claimed or now - m.get("ended", now) < 7 * 86400}

    def _teams(self, s, ids, team_size, mode, now):
        """Anchor the oldest party; search a bounded window without splitting parties."""
        if not team_size:
            return [ids[:1], []]
        size = lambda group: sum(len(s["parties"][p]["members"]) for p in group)
        rating = lambda group: sum(s["users"][u]["ratings"][mode] for p in group for u in s["parties"][p]["members"]) / team_size
        # Include enough solo parties for 5v5 while keeping the search bounded.
        ids = ids[:12]
        best = None
        for count in range(1, team_size + 1):
            for rest in combinations(ids[1:], count - 1):
                left = [ids[0], *rest]
                if size(left) != team_size:
                    continue
                other = [p for p in ids if p not in left]
                for n in range(1, team_size + 1):
                    for right in combinations(other, n):
                        if size(right) != team_size:
                            continue
                        gap = abs(rating(left) - rating(right))
                        allowed = min(1000, 150 + 5 * (now - s["queue"][ids[0]]["created"]))
                        if gap <= allowed and (best is None or gap < best[0]):
                            best = (gap, [left, list(right)])
        return best[1] if best else None

    def tick(self):
        with self.transaction() as s:
            self._expire(s)
            now = self.clock()
            for server in s["servers"].values():
                if now - server["seen"] > SERVER_SECONDS:
                    continue
                if server["match"]:
                    m = s["matches"][server["match"]]
                    if m["mode"] == "casual" and m["state"] == "active":
                        for pid, q in sorted(list(s["queue"].items()), key=lambda entry: entry[1]["created"]):
                            members = s["parties"][pid]["members"]
                            if q["mode"] != "casual" or q["region"] != server["region"] or len(m["players"]) + len(members) > server["capacity"]:
                                continue
                            for uid in members:
                                m["players"][uid] = {"team": 0, "accepted": True, "join_token": secrets.token_urlsafe(32)}
                                s["users"][uid]["match"] = m["id"]
                            s["parties"][pid]["ready"] = []
                            del s["queue"][pid]
                    continue
                for mode in server["modes"]:
                    if MODES[mode]["team_size"] * 2 > server["capacity"]:
                        continue
                    ids = sorted((p for p, q in s["queue"].items() if q["mode"] == mode and q["region"] == server["region"]), key=lambda p: s["queue"][p]["created"])
                    ids = [p for p in ids if len(s["parties"][p]["members"]) <= server["capacity"]]
                    if not ids:
                        continue
                    teams = self._teams(s, ids, MODES[mode]["team_size"], mode, now)
                    if not teams:
                        continue
                    mid = identifier()
                    players = {u: {"team": team, "accepted": mode == "casual", "join_token": secrets.token_urlsafe(32)} for team, group in enumerate(teams) for pid in group for u in s["parties"][pid]["members"]}
                    s["matches"][mid] = {"id": mid, "server": server["id"], "mode": mode, "map": MODES[mode]["map"], "state": "starting" if mode == "casual" else "ready_check", "deadline": now + (START_SECONDS if mode == "casual" else READY_SECONDS), "players": players, "reason": None, "result": None}
                    server["match"] = mid
                    for group in teams:
                        for pid in group:
                            del s["queue"][pid]
                            s["parties"][pid]["ready"] = []
                    for uid in players:
                        s["users"][uid]["match"] = mid
                    break

    def confirm(self, actor, match_id, accept):
        with self.transaction() as s:
            self._expire(s)
            m = s["matches"].get(match_id)
            if not m or actor not in m["players"]:
                raise Rejected("Match not found", 404)
            if m["state"] != "ready_check":
                if accept and m["state"] in {"starting", "active"}:
                    return
                raise Rejected("This ready check has ended")
            if not accept:
                self._cancel(s, m, "A player declined the match")
                return
            m["players"][actor]["accepted"] = True
            if all(p["accepted"] for p in m["players"].values()):
                m["state"], m["deadline"] = "starting", self.clock() + START_SECONDS

    def heartbeat(self, server_id, running_match):
        with self.transaction() as s:
            self._expire(s)
            server = s["servers"].get(server_id)
            if server is None:
                # Configuration can change after an in-flight request has
                # authenticated in another worker.
                raise Rejected("Server is no longer configured", 401)
            mid = server["match"]
            if mid and s["matches"][mid]["state"] in TERMINAL and running_match is None:
                server["match"] = mid = None
            if running_match and running_match != mid:
                raise Rejected("Stop the stale server match before claiming another")
            if mid and s["matches"][mid]["state"] == "active" and running_match != mid:
                self._cancel(s, s["matches"][mid], "Dedicated server restarted")
            server["seen"] = self.clock()
            if not mid:
                return {"match": None}
            m = s["matches"][mid]
            return {"match": {"id": mid, "state": m["state"], "mode": m["mode"], "map": m["map"], "rules": m.get("rules", {}), "players": [{"steam_id": u, "team": p["team"]} for u, p in m["players"].items()]}}

    def server_ready(self, server_id, match_id):
        with self.transaction() as s:
            self._expire(s)
            m = self._server_match(s, server_id, match_id)
            if m["state"] == "active":
                return
            if m["state"] != "starting":
                raise Rejected("Match is not awaiting server startup")
            m["state"], m["deadline"] = "active", self.clock() + MATCH_SECONDS

    def _server_match(self, s, server_id, match_id):
        m = s["matches"].get(match_id)
        if not m or m["server"] != server_id:
            raise Rejected("Match is not assigned to this server", 403)
        return m

    def abort_match(self, server_id, match_id, reason):
        reasons = {"players_missing": "Players did not reach the server", "server_error": "The dedicated server could not continue"}
        if reason not in reasons:
            raise Rejected("Invalid cancellation reason", 400)
        with self.transaction() as s:
            m = self._server_match(s, server_id, match_id)
            if m["state"] == "cancelled":
                return
            if m["state"] == "finished":
                raise Rejected("A completed result cannot be cancelled")
            self._cancel(s, m, reasons[reason])

    def authorize_join(self, server_id, match_id, steam_id, token):
        with self.transaction() as s:
            self._expire(s)
            m = self._server_match(s, server_id, match_id)
            p = m["players"].get(steam_id)
            if m["state"] != "active" or not p or not secrets.compare_digest(p["join_token"], token):
                raise Rejected("Player does not hold a valid match reservation", 403)
            return {"steam_id": steam_id, "team": p["team"]}

    def report_result(self, server_id, match_id, winner):
        if winner not in (None, 0, 1) or isinstance(winner, bool):
            raise Rejected("Invalid winning team", 400)
        with self.transaction() as s:
            self._expire(s)
            m = self._server_match(s, server_id, match_id)
            if m["state"] == "finished":
                if m["result"]["winner"] != winner:
                    raise Rejected("Conflicting duplicate result")
                return m["result"]
            if m["state"] != "active":
                raise Rejected("Match is not active")
            delta = 0
            if MODES[m["mode"]]["ranked"]:
                teams = [[u for u, p in m["players"].items() if p["team"] == t] for t in (0, 1)]
                ratings = [sum(s["users"][u]["ratings"][m["mode"]] for u in team) / len(team) for team in teams]
                expected = 1 / (1 + 10 ** (max(-4000, min(4000, ratings[1] - ratings[0])) / 400))
                score = 0.5 if winner is None else (1 if winner == 0 else 0)
                delta = round(32 * (score - expected))
                for uid, player in m["players"].items():
                    s["users"][uid]["ratings"][m["mode"]] += delta if player["team"] == 0 else -delta
            m["state"] = "finished"
            m["ended"] = self.clock()
            m["result"] = {"winner": winner, "rating_change_team_0": delta}
            for uid in m["players"]:
                if s["users"][uid]["match"] == match_id:
                    s["users"][uid]["match"] = None
                    ranked = MODES[m["mode"]]["ranked"]
                    team = m["players"][uid]["team"]
                    s["users"][uid]["last_match"] = {
                        **self._match_summary(s, m, uid, "finished"), "result": m["result"],
                        "outcome": ("draw" if winner is None else "win" if winner == team else "loss") if ranked else "completed",
                        "rating_change": (delta if team == 0 else -delta) if ranked else 0,
                    }
            return m["result"]

    def leave_casual(self, actor):
        with self.transaction() as s:
            mid = s["users"][actor]["match"]
            if mid is None:
                return
            m = s["matches"][mid]
            if MODES[m["mode"]]["ranked"]:
                raise Rejected("Competitive matches must be completed or resolved by the server")
            self._leave_casual(s, m, actor)

    def _leave_casual(self, s, match, actor):
        match["players"].pop(actor, None)
        if s["users"][actor]["match"] == match["id"]:
            s["users"][actor]["match"] = None
            s["users"][actor]["last_match"] = {**self._match_summary(s, match, actor, "left"), "reason": "You left the unranked match"}
        if not match["players"] and match["state"] not in TERMINAL:
            self._cancel(s, match, "Casual server is empty")

    def casual_disconnected(self, server_id, match_id, actor):
        with self.transaction() as s:
            m = self._server_match(s, server_id, match_id)
            if MODES[m["mode"]]["ranked"]:
                raise Rejected("Competitive disconnects require match resolution")
            if m["state"] in TERMINAL:
                return
            if actor in m["players"]:
                self._leave_casual(s, m, actor)

    def status(self, actor):
        with self.transaction() as s:
            self._expire(s)
            user = s["users"][actor]
            user["seen"] = self.clock()
            party = self._party(s, actor)
            p = {k: v for k, v in party.items() if k != "invite"}
            if party["leader"] == actor:
                p["invite"] = party["invite"]
            match = None
            if user["match"]:
                m = s["matches"][user["match"]]
                match = {k: m[k] for k in ("id", "mode", "state", "map", "deadline")}
                match["rules"] = m.get("rules", {})
                match["seconds_remaining"] = max(0, math.ceil(m["deadline"] - self.clock()))
                match["accepted"] = sum(p["accepted"] for p in m["players"].values())
                match["players"] = len(m["players"])
                match["you_accepted"] = m["players"][actor]["accepted"]
                if m["state"] == "active":
                    server = s["servers"][m["server"]]
                    match["connection"] = {"address": server["address"], "port": server["port"], "join_token": m["players"][actor]["join_token"]}
            return {"protocol": PROTOCOL, "steam_id": actor, "party": p, "queue": s["queue"].get(party["id"]), "match": match, "last_match": user.get("last_match"), "ratings": user["ratings"]}

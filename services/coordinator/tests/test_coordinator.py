from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import tempfile
import unittest
from unittest.mock import Mock, patch

from fastapi.testclient import TestClient

from services.coordinator.api import Settings, create_app, verify_steam_ticket
from services.coordinator.core import Coordinator, Rejected


SERVER = dict(id="eu-1", token="test-server-token-" + "a" * 32, address="127.0.0.1", port=27015, region="eu", modes=["duel", "team", "casual"])


class CoordinatorTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.path = Path(self.directory.name) / "state.sqlite3"
        self.now = 1000.0
        self.core = Coordinator(self.path, [SERVER], lambda: self.now)
        self.core.heartbeat("eu-1", None)
        self.users = [str(76561198000000000 + n) for n in range(12)]
        self.tokens = {u: self.core.login(u)["token"] for u in self.users}

    def tearDown(self):
        self.core.close()
        self.directory.cleanup()

    def duel(self):
        for u in self.users[:2]:
            self.core.enqueue(u, "duel", "eu", 1)
        self.core.tick()
        return self.core.status(self.users[0])["match"]["id"]

    def active_duel(self):
        mid = self.duel()
        for u in self.users[:2]:
            self.core.confirm(u, mid, True)
        self.core.server_ready("eu-1", mid)
        return mid

    def test_duel_ready_gate_join_authorization_and_idempotent_rating(self):
        mid = self.duel()
        self.assertNotIn("connection", self.core.status(self.users[0])["match"])
        self.core.confirm(self.users[0], mid, True)
        with self.assertRaises(Rejected):
            self.core.server_ready("eu-1", mid)
        self.core.confirm(self.users[1], mid, True)
        self.core.server_ready("eu-1", mid)
        conn = self.core.status(self.users[0])["match"]["connection"]
        self.assertEqual(self.core.authorize_join("eu-1", mid, self.users[0], conn["join_token"])["team"], 0)
        with self.assertRaises(Rejected):
            self.core.authorize_join("eu-1", mid, self.users[1], conn["join_token"])
        with self.assertRaises(Rejected):
            self.core.authorize_join("eu-1", mid, self.users[2], conn["join_token"])
        self.core.report_result("eu-1", mid, 0)
        self.core.report_result("eu-1", mid, 0)
        self.assertEqual(self.core.status(self.users[0])["ratings"]["duel"], 1516)
        self.assertEqual(self.core.status(self.users[1])["ratings"]["duel"], 1484)
        with self.assertRaises(Rejected):
            self.core.report_result("eu-1", mid, 1)
        with self.assertRaises(Rejected):
            self.core.authorize_join("eu-1", mid, self.users[0], conn["join_token"])

    def test_parties_never_split_and_readiness_is_required(self):
        for leader, member in [(self.users[0], self.users[1]), (self.users[2], self.users[3])]:
            invitation = self.core.status(leader)["party"]["invite"]
            self.core.party_join(member, invitation)
            with self.assertRaises(Rejected):
                self.core.enqueue(leader, "team", "eu", 1)
            for u in (leader, member):
                self.core.party_ready(u, True)
            with self.assertRaises(Rejected):
                self.core.enqueue(member, "team", "eu", 1)
            with self.assertRaises(Rejected):
                self.core.enqueue(leader, "duel", "eu", 1)
            self.core.enqueue(leader, "team", "eu", 1)
        self.core.tick()
        command = self.core.heartbeat("eu-1", None)["match"]
        teams = {p["steam_id"]: p["team"] for p in command["players"]}
        self.assertEqual(teams[self.users[0]], teams[self.users[1]])
        self.assertEqual(teams[self.users[2]], teams[self.users[3]])
        self.assertNotEqual(teams[self.users[0]], teams[self.users[2]])
        with self.assertRaises(Rejected):
            self.core.party_leave(self.users[0])

    def test_cancel_from_party_member_withdraws_entire_party(self):
        invitation = self.core.status(self.users[0])["party"]["invite"]
        self.core.party_join(self.users[1], invitation)
        for u in self.users[:2]:
            self.core.party_ready(u, True)
        self.core.enqueue(self.users[0], "team", "eu", 1)
        self.core.cancel_queue(self.users[1])
        self.assertIsNone(self.core.status(self.users[0])["queue"])

    def test_decline_holds_server_until_shutdown_ack(self):
        mid = self.duel()
        self.core.confirm(self.users[0], mid, False)
        self.assertIsNone(self.core.status(self.users[1])["match"])
        self.assertIn("declined", self.core.status(self.users[1])["last_match"]["reason"])
        for u in self.users[2:4]:
            self.core.enqueue(u, "duel", "eu", 1)
        self.core.tick()
        self.assertIsNone(self.core.status(self.users[2])["match"])
        self.core.heartbeat("eu-1", None)
        self.core.tick()
        self.assertIsNotNone(self.core.status(self.users[2])["match"])

    def test_cancel_racing_allocation_declines_ready_check(self):
        mid = self.duel()
        self.assertEqual(self.core.status(self.users[0])["match"]["seconds_remaining"], 30)
        self.now += 2.2
        self.assertEqual(self.core.status(self.users[0])["match"]["seconds_remaining"], 28)
        self.core.cancel_queue(self.users[0])
        self.assertIsNone(self.core.status(self.users[1])["match"])
        self.assertEqual(self.core.heartbeat("eu-1", mid)["match"]["state"], "cancelled")

    def test_cancel_cannot_abort_an_active_competitive_match(self):
        self.active_duel()
        with self.assertRaises(Rejected):
            self.core.cancel_queue(self.users[0])
        self.assertEqual(self.core.status(self.users[0])["match"]["state"], "active")

    def test_presence_expiry_removes_queue_and_ready_check_expires(self):
        self.core.enqueue(self.users[0], "duel", "eu", 1)
        self.now += 46
        self.core.tick()
        self.assertIsNone(self.core.status(self.users[0])["queue"])
        self.core.status(self.users[1])
        self.core.heartbeat("eu-1", None)
        mid = self.duel()
        self.now += 25
        self.core.heartbeat("eu-1", None)
        self.now += 6
        self.core.tick()
        self.assertIsNone(self.core.status(self.users[0])["match"])
        with self.assertRaises(Rejected):
            self.core.confirm(self.users[1], mid, True)

    def test_server_loss_cancels_without_rating_changes(self):
        self.active_duel()
        self.now += 31
        self.core.tick()
        self.assertIsNone(self.core.status(self.users[0])["match"])
        self.assertEqual(self.core.status(self.users[0])["ratings"]["duel"], 1500)

    def test_persistence_and_concurrent_allocation(self):
        for u in self.users[:4]:
            self.core.enqueue(u, "duel", "eu", 1)
        other = Coordinator(self.path, clock=lambda: self.now)
        try:
            with ThreadPoolExecutor(max_workers=2) as pool:
                list(pool.map(lambda c: c.tick(), [self.core, other]))
            mids = {self.core.status(u)["match"]["id"] for u in self.users[:4] if self.core.status(u)["match"]}
            self.assertEqual(len(mids), 1)
            self.assertEqual(sum(self.core.status(u)["match"] is not None for u in self.users[:4]), 2)
            self.assertEqual(other.authenticate(self.tokens[self.users[0]]), self.users[0])
        finally:
            other.close()

    def test_casual_backfill_leave_and_no_ratings(self):
        self.core.enqueue(self.users[0], "casual", "eu", 1)
        self.core.tick()
        mid = self.core.status(self.users[0])["match"]["id"]
        self.core.server_ready("eu-1", mid)
        self.core.enqueue(self.users[1], "casual", "eu", 1)
        self.core.tick()
        self.assertEqual(self.core.status(self.users[1])["match"]["id"], mid)
        self.core.leave_casual(self.users[0])
        self.assertIsNone(self.core.status(self.users[0])["match"])
        self.core.report_result("eu-1", mid, None)
        self.assertEqual(self.core.status(self.users[1])["ratings"], {"duel": 1500, "team": 1500})

    def test_queue_idempotency_and_session_rotation(self):
        self.core.enqueue(self.users[0], "duel", "eu", 1)
        created = self.core.status(self.users[0])["queue"]["created"]
        self.now += 1
        self.core.enqueue(self.users[0], "duel", "eu", 1)
        self.assertEqual(self.core.status(self.users[0])["queue"]["created"], created)
        self.core.login(self.users[0], "ticket-fingerprint")
        with self.assertRaises(Rejected):
            self.core.authenticate(self.tokens[self.users[0]])
        with self.assertRaises(Rejected):
            self.core.login(self.users[0], "ticket-fingerprint")

    def test_player_cannot_act_as_dedicated_server(self):
        with self.assertRaises(Rejected):
            self.core.authenticate_server(self.tokens[self.users[0]])
        mid = self.active_duel()
        with self.assertRaises(Rejected):
            self.core.report_result("another-server", mid, 0)


class APITests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.core = Coordinator(Path(self.directory.name) / "api.sqlite3", [SERVER])
        self.app = create_app(self.core)

    def tearDown(self):
        self.core.close()
        self.directory.cleanup()

    def test_missing_credentials_invalid_payloads_and_development_gate(self):
        with TestClient(self.app) as client:
            self.assertEqual(client.get("/v1/me").status_code, 401)
            self.assertEqual(client.post("/v1/auth/steam", json={"ticket": "00"}).status_code, 503)
            self.assertEqual(client.post("/v1/auth/steam", json={"ticket": "not-hex"}).status_code, 422)
            self.assertEqual(client.post("/v1/auth/development", json={"steam_id": "1", "key": "x" * 32}).status_code, 403)
            self.assertEqual(client.post("/v1/auth/steam", content=b"x" * 17000).status_code, 413)
            self.assertEqual(client.get("/v1/health").status_code, 200)

    def test_development_login_requires_loopback_and_secret(self):
        app = create_app(self.core, Settings(development_key="x" * 32))
        for host, code in [("127.0.0.1", 200), ("192.0.2.1", 403)]:
            with TestClient(app, client=(host, 50000)) as client:
                response = client.post("/v1/auth/development", json={"steam_id": "76561198000000001", "key": "x" * 32})
                self.assertEqual(response.status_code, code)
                if code == 200:
                    token = response.json()["token"]
                    self.assertEqual(client.get("/v1/me", headers={"Authorization": "Bearer " + token}).status_code, 200)

    def test_authenticated_http_duel(self):
        app = create_app(self.core, steam_verifier=lambda ticket, settings: str(76561198000000000 + int(ticket, 16)))
        with TestClient(app) as client:
            player_headers = []
            for ticket in ("01", "02"):
                response = client.post("/v1/auth/steam", json={"ticket": ticket})
                self.assertEqual(response.status_code, 200)
                player_headers.append({"Authorization": "Bearer " + response.json()["token"]})
            server = {"Authorization": "Bearer " + SERVER["token"]}
            self.assertEqual(client.post("/v1/servers/heartbeat", json={}, headers=server).status_code, 200)
            for headers in player_headers:
                self.assertEqual(client.post("/v1/queue", json={"mode": "duel", "region": "eu", "protocol": 1}, headers=headers).status_code, 200)
            self.core.tick()
            mid = client.get("/v1/me", headers=player_headers[0]).json()["match"]["id"]
            for headers in player_headers:
                self.assertEqual(client.post(f"/v1/matches/{mid}/confirm", json={"accept": True}, headers=headers).status_code, 200)
            self.assertEqual(client.post(f"/v1/servers/matches/{mid}/ready", headers=server).status_code, 200)
            self.assertEqual(client.post(f"/v1/servers/matches/{mid}/result", json={"winner": 0}, headers=player_headers[0]).status_code, 401)
            self.assertEqual(client.post(f"/v1/servers/matches/{mid}/result", json={"winner": 0}, headers=server).status_code, 200)

    def test_steam_verification_uses_server_config_and_rejects_bans(self):
        settings = Settings(123, "private-key")
        response = Mock()
        response.json.return_value = {"response": {"params": {"result": "OK", "steamid": "76561198000000001"}}}
        with patch("services.coordinator.api.httpx.get", return_value=response) as get:
            self.assertEqual(verify_steam_ticket("ab", settings), "76561198000000001")
            self.assertEqual(get.call_args.kwargs["params"]["appid"], 123)
            self.assertEqual(get.call_args.kwargs["params"]["identity"], "hl2dm-matchmaking")
            response.json.return_value["response"]["params"]["publisherbanned"] = True
            with self.assertRaises(Rejected):
                verify_steam_ticket("ab", settings)


if __name__ == "__main__":
    unittest.main()

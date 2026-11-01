from pathlib import Path
import tempfile
import unittest

from fastapi.testclient import TestClient

from services.coordinator.api import create_app, Settings
from services.coordinator.core import Coordinator, MODES, Rejected


class ExpandedModesTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.path = Path(self.directory.name) / "state.sqlite3"
        self.server = dict(id="test", token="test-server-" + "x" * 32, address="127.0.0.1", port=27015,
                           region="eu", modes=list(MODES), capacity=16)
        self.core = Coordinator(self.path, [self.server], lambda: 1000)
        self.core.heartbeat("test", None)
        self.users = [str(76561198000000000 + i) for i in range(12)]
        self.tokens = {u: self.core.login(u)["token"] for u in self.users}

    def tearDown(self):
        self.core.close()
        self.directory.cleanup()

    def group(self, users):
        invitation = self.core.status(users[0])["party"]["invite"]
        for user in users[1:]:
            self.core.party_join(user, invitation)
        for user in users:
            self.core.party_ready(user, True)

    def test_three_four_five_team_solo_matches_and_ratings(self):
        for size in (3, 4, 5):
            mode = f"team{size}"
            for user in self.users[:size * 2]:
                self.core.enqueue(user, mode, "eu", 1)
            self.core.tick()
            match = self.core.heartbeat("test", None)["match"]
            self.assertEqual(match["mode"], mode)
            for team in (0, 1):
                self.assertEqual(sum(p["team"] == team for p in match["players"]), size)
            for user in self.users[:size * 2]:
                self.core.confirm(user, match["id"], True)
            self.core.server_ready("test", match["id"])
            self.core.report_result("test", match["id"], 0)
            for player in match["players"]:
                self.assertEqual(self.core.status(player["steam_id"])["ratings"][mode], 1516 if player["team"] == 0 else 1484)
            self.core.heartbeat("test", None)

    def test_mixed_five_player_teams_do_not_split_parties(self):
        self.group(self.users[:3])
        self.group(self.users[3:5])
        self.group(self.users[5:9])
        for i in (0, 3, 5, 9):
            self.core.enqueue(self.users[i], "team5", "eu", 1)
        self.core.tick()
        players = self.core.heartbeat("test", None)["match"]["players"]
        teams = {p["steam_id"]: p["team"] for p in players}
        for group in (self.users[:3], self.users[3:5], self.users[5:9]):
            self.assertEqual(len({teams[u] for u in group}), 1)
        self.assertEqual(sum(t == 0 for t in teams.values()), 5)

    def test_ten_member_party_and_ranked_limit(self):
        self.group(self.users[:10])
        invitation = self.core.status(self.users[0])["party"]["invite"]
        with self.assertRaises(Rejected):
            self.core.party_join(self.users[10], invitation)
        with self.assertRaises(Rejected):
            self.core.enqueue(self.users[0], "team5", "eu", 1)
        self.core.custom_match(self.users[0], "eu", "dm_lockdown", 5, 20, 50, 1)
        self.assertEqual(self.core.status(self.users[0])["match"]["players"], 10)

    def test_pool_capacity_and_rating_migration(self):
        with self.core.transaction() as state:
            state["users"][self.users[0]]["ratings"] = {"duel": 1640, "team": 1450}
        self.core.close()
        self.core = Coordinator(self.path, [{**self.server, "capacity": 6}], lambda: 1000)
        ratings = self.core.status(self.users[0])["ratings"]
        self.assertEqual((ratings["duel"], ratings["team"], ratings["team5"]), (1640, 1450, 1500))
        with self.assertRaises(Rejected):
            self.core.enqueue(self.users[0], "team5", "eu", 1)
        self.assertIsNone(self.core.status(self.users[0])["queue"])

    def test_private_match_consent_rules_reservations_and_no_public_backfill(self):
        self.group(self.users[:3])
        with self.assertRaises(Rejected):
            self.core.custom_match(self.users[1], "eu", "dm_lockdown", 2, 30, 100, 1)
        self.core.party_ready(self.users[1], False)
        with self.assertRaises(Rejected):
            self.core.custom_match(self.users[0], "eu", "dm_lockdown", 2, 30, 100, 1)
        self.core.party_ready(self.users[1], True)
        self.core.custom_match(self.users[0], "eu", "dm_lockdown", 2, 30, 100, 1)
        match = self.core.status(self.users[0])["match"]
        self.assertEqual(match["state"], "ready_check")
        self.assertEqual(match["rules"], {"team_size": 2, "time_limit": 30, "frag_limit": 100})
        with self.assertRaises(Rejected):
            self.core.confirm(self.users[3], match["id"], True)
        for user in self.users[:3]:
            self.core.confirm(user, match["id"], True)
        self.assertEqual(self.core.heartbeat("test", None)["match"]["rules"], match["rules"])
        self.core.server_ready("test", match["id"])
        self.core.enqueue(self.users[3], "casual", "eu", 1)
        self.core.tick()
        self.assertIsNone(self.core.status(self.users[3])["match"])
        self.core.leave_casual(self.users[2])
        self.core.report_result("test", match["id"], 0)
        for user in self.users[:3]:
            self.assertTrue(all(r == 1500 for r in self.core.status(user)["ratings"].values()))

    def test_private_settings_and_stale_roster_rejected(self):
        for map_name, size, minutes, frags in (("../evil", 0, 20, 0), ("dm_lockdown", 6, 20, 0),
                ("dm_lockdown", 0, 0, 0), ("dm_lockdown", 0, 20, -1), ("dm_lockdown", 1, 20, 0)):
            with self.assertRaises(Rejected):
                self.core.custom_match(self.users[0], "eu", map_name, size, minutes, frags, 1)
        party = self.core.status(self.users[0])["party"]
        with self.assertRaises(Rejected):
            self.core.custom_match(self.users[0], "eu", "dm_lockdown", 0, 20, 0, 1, party["id"], party["revision"] + 1)
        with self.assertRaises(Rejected):
            self.core.enqueue(self.users[0], "custom", "eu", 1)
        with self.core.transaction() as state:
            state["servers"]["test"]["maps"] = ["dm_lockdown"]
        with self.assertRaises(Rejected):
            self.core.custom_match(self.users[0], "eu", "dm_overwatch", 0, 20, 0, 1)

    def test_custom_http_auth_and_schema(self):
        app = create_app(self.core, Settings())
        body = dict(region="eu", map_name="dm_lockdown", team_size=0, time_limit=10, frag_limit=25, protocol=1)
        headers = {"Authorization": "Bearer " + self.tokens[self.users[0]]}
        with TestClient(app) as client:
            self.assertEqual(client.post("/v1/custom/matches", json=body).status_code, 401)
            self.assertEqual(client.post("/v1/custom/matches", json={**body, "time_limit": True}, headers=headers).status_code, 422)
            response = client.post("/v1/custom/matches", json=body, headers=headers)
            self.assertEqual(response.status_code, 200, response.text)
            self.assertEqual(response.json()["match"]["rules"]["frag_limit"], 25)


if __name__ == "__main__":
    unittest.main()

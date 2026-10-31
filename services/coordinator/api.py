"""HTTP boundary: Steam-authenticated players and independently credentialed servers."""
from __future__ import annotations

import asyncio
from collections import defaultdict, deque
from contextlib import asynccontextmanager, suppress
from dataclasses import dataclass
import ipaddress
import secrets
import time
from typing import Literal

from fastapi import Depends, FastAPI, Header, Request
from fastapi.responses import JSONResponse
import httpx
from pydantic import BaseModel, ConfigDict, Field

from .core import Coordinator, PROTOCOL, Rejected, digest


@dataclass(frozen=True)
class Settings:
    steam_app_id: int = 0
    steam_publisher_key: str = ""
    ticket_identity: str = "hl2dm-matchmaking"
    development_key: str = ""


class Input(BaseModel):
    model_config = ConfigDict(extra="forbid", strict=True)


class SteamLogin(Input):
    ticket: str = Field(min_length=2, max_length=5120, pattern=r"^(?:[0-9a-fA-F]{2})+$")


class DevelopmentLogin(Input):
    steam_id: str = Field(pattern=r"^[0-9]{1,20}$")
    key: str = Field(min_length=32, max_length=128)


class PartyJoin(Input):
    invitation: str = Field(min_length=16, max_length=128)


class Ready(Input):
    ready: bool
    party_id: str | None = Field(default=None, pattern=r"^[a-f0-9]{32}$")
    revision: int | None = Field(default=None, ge=0)


class PartyLobby(Input):
    lobby_id: str = Field(pattern=r"^[0-9]{1,20}$")
    members: list[str] = Field(min_length=1, max_length=4)
    party_id: str = Field(pattern=r"^[a-f0-9]{32}$")
    revision: int = Field(ge=0)


class Queue(Input):
    mode: Literal["duel", "team", "casual"]
    region: str = Field(min_length=1, max_length=32, pattern=r"^[a-z0-9-]+$")
    protocol: int
    party_id: str | None = Field(default=None, pattern=r"^[a-f0-9]{32}$")
    revision: int | None = Field(default=None, ge=0)


class Confirm(Input):
    accept: bool


class Heartbeat(Input):
    running_match: str | None = Field(default=None, pattern=r"^[a-f0-9]{32}$")


class JoinAuthorization(Input):
    steam_id: str = Field(pattern=r"^[0-9]{1,20}$")
    join_token: str = Field(min_length=32, max_length=128)


class Result(Input):
    winner: int | None = Field(default=None, ge=0, le=1)


class Abort(Input):
    reason: Literal["players_missing", "server_error"]


class RequestLimits:
    """Bound bodies before JSON parsing; rate-limit by peer address, never a user header."""
    def __init__(self, app):
        self.app = app
        self.requests = defaultdict(deque)

    async def __call__(self, scope, receive, send):
        if scope["type"] != "http":
            return await self.app(scope, receive, send)
        now = time.monotonic()
        address = (scope.get("client") or ("unknown",))[0]
        auth = scope["path"].startswith("/v1/auth/")
        key = (address, auth)
        if len(self.requests) > 4096:
            self.requests = defaultdict(deque, {k: q for k, q in self.requests.items() if q and q[-1] > now - 60})
            if len(self.requests) > 4096 and key not in self.requests:
                return await JSONResponse({"error": "Service busy"}, 429)(scope, receive, send)
        times = self.requests[key]
        while times and times[0] <= now - 60:
            times.popleft()
        if len(times) >= (20 if auth else 300):
            return await JSONResponse({"error": "Too many requests"}, 429)(scope, receive, send)
        times.append(now)
        body = bytearray()
        while True:
            message = await receive()
            if message["type"] == "http.disconnect":
                return
            body.extend(message.get("body", b""))
            if len(body) > 16384:
                return await JSONResponse({"error": "Request too large"}, 413)(scope, receive, send)
            if not message.get("more_body", False):
                break
        delivered = False

        async def bounded_receive():
            nonlocal delivered
            if not delivered:
                delivered = True
                return {"type": "http.request", "body": bytes(body), "more_body": False}
            return await receive()

        await self.app(scope, bounded_receive, send)


def verify_steam_ticket(ticket, settings):
    if settings.steam_app_id <= 0 or not settings.steam_publisher_key:
        raise Rejected("Steam sign-in is not configured on this coordinator", 503)
    try:
        response = httpx.get(
            "https://partner.steam-api.com/ISteamUserAuth/AuthenticateUserTicket/v1/",
            params={"key": settings.steam_publisher_key, "appid": settings.steam_app_id,
                    "ticket": ticket, "identity": settings.ticket_identity},
            timeout=10, follow_redirects=False,
        )
        response.raise_for_status()
        params = response.json().get("response", {}).get("params", {})
    except (httpx.HTTPError, ValueError):
        # Do not expose/log the upstream URL: it contains the publisher key and ticket.
        raise Rejected("Steam authentication is temporarily unavailable", 503) from None
    if params.get("result") != "OK" or not params.get("steamid"):
        raise Rejected("Steam rejected this authentication ticket", 401)
    if params.get("publisherbanned") or params.get("vacbanned"):
        raise Rejected("This account cannot use matchmaking", 403)
    return str(params["steamid"])


def create_app(core: Coordinator, settings=Settings(), steam_verifier=verify_steam_ticket):
    if settings.development_key and len(settings.development_key) < 32:
        raise ValueError("The local development key must contain at least 32 characters")

    @asynccontextmanager
    async def lifespan(app):
        async def match_loop():
            while True:
                await asyncio.to_thread(core.tick)
                await asyncio.sleep(1)
        task = asyncio.create_task(match_loop())
        app.state.match_loop = task
        try:
            yield
        finally:
            task.cancel()
            with suppress(asyncio.CancelledError):
                await task

    app = FastAPI(title="HL2DM Matchmaking Coordinator", version="0.1.0", lifespan=lifespan)
    app.add_middleware(RequestLimits)

    @app.exception_handler(Rejected)
    async def rejected(request, error):
        return JSONResponse({"error": error.message}, status_code=error.status)

    def bearer(authorization: str = Header(default="")):
        if not authorization.startswith("Bearer ") or len(authorization) > 256:
            raise Rejected("A bearer token is required", 401)
        return authorization[7:]

    def player(token=Depends(bearer)):
        return core.authenticate(token)

    def server(token=Depends(bearer)):
        return core.authenticate_server(token)

    @app.get("/v1/health")
    def health():
        task = getattr(app.state, "match_loop", None)
        if task is None or task.done():
            raise Rejected("Matchmaking worker is unavailable", 503)
        return {"protocol": PROTOCOL, "steam_configured": bool(settings.steam_app_id and settings.steam_publisher_key), "development": bool(settings.development_key)}

    @app.post("/v1/auth/steam")
    def steam_login(body: SteamLogin):
        identity = steam_verifier(body.ticket, settings)
        return core.login(identity, digest(body.ticket.lower()))

    @app.post("/v1/auth/development")
    def development_login(body: DevelopmentLogin, request: Request):
        peer = request.client.host if request.client else ""
        try:
            loopback = ipaddress.ip_address(peer).is_loopback
        except ValueError:
            loopback = False
        if not settings.development_key or not loopback or not secrets.compare_digest(body.key, settings.development_key):
            raise Rejected("Development login is disabled", 403)
        return core.login(body.steam_id)

    @app.get("/v1/me")
    def status(actor=Depends(player)):
        return core.status(actor)

    @app.post("/v1/party/join")
    def party_join(body: PartyJoin, actor=Depends(player)):
        core.party_join(actor, body.invitation)
        return core.status(actor)

    @app.post("/v1/party/leave")
    def party_leave(actor=Depends(player)):
        core.party_leave(actor)
        return core.status(actor)

    @app.post("/v1/party/ready")
    def party_ready(body: Ready, actor=Depends(player)):
        core.party_ready(actor, body.ready, body.party_id, body.revision)
        return core.status(actor)

    @app.post("/v1/party/lobby")
    def party_lobby(body: PartyLobby, actor=Depends(player)):
        core.party_lobby(actor, body.lobby_id, body.members, body.party_id, body.revision)
        return core.status(actor)

    @app.post("/v1/queue")
    def queue(body: Queue, actor=Depends(player)):
        core.enqueue(actor, body.mode, body.region, body.protocol, body.party_id, body.revision)
        return core.status(actor)

    @app.delete("/v1/queue")
    def cancel_queue(actor=Depends(player)):
        core.cancel_queue(actor)
        return core.status(actor)

    @app.post("/v1/matches/{match_id}/confirm")
    def confirm(match_id: str, body: Confirm, actor=Depends(player)):
        core.confirm(actor, match_id, body.accept)
        return core.status(actor)

    @app.post("/v1/casual/leave")
    def leave_casual(actor=Depends(player)):
        core.leave_casual(actor)
        return core.status(actor)

    @app.post("/v1/servers/heartbeat")
    def heartbeat(body: Heartbeat, server_id=Depends(server)):
        return core.heartbeat(server_id, body.running_match)

    @app.post("/v1/servers/matches/{match_id}/ready")
    def server_ready(match_id: str, server_id=Depends(server)):
        core.server_ready(server_id, match_id)
        return {"ok": True}

    @app.post("/v1/servers/matches/{match_id}/authorize")
    def authorize_join(match_id: str, body: JoinAuthorization, server_id=Depends(server)):
        return core.authorize_join(server_id, match_id, body.steam_id, body.join_token)

    @app.post("/v1/servers/matches/{match_id}/result")
    def result(match_id: str, body: Result, server_id=Depends(server)):
        return core.report_result(server_id, match_id, body.winner)

    @app.post("/v1/servers/matches/{match_id}/abort")
    def abort(match_id: str, body: Abort, server_id=Depends(server)):
        core.abort_match(server_id, match_id, body.reason)
        return {"ok": True}

    @app.post("/v1/servers/matches/{match_id}/players/{steam_id}/left")
    def casual_disconnected(match_id: str, steam_id: str, server_id=Depends(server)):
        core.casual_disconnected(server_id, match_id, steam_id)
        return {"ok": True}

    return app

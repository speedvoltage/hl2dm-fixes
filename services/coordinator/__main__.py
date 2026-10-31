import argparse
import ipaddress
import json
import os
from pathlib import Path

import uvicorn

from .api import Settings, create_app
from .core import Coordinator


def main():
    parser = argparse.ArgumentParser(description="Self-hosted HL2DM matchmaking coordinator")
    parser.add_argument("--database", default="artifacts/coordinator.sqlite3")
    parser.add_argument("--servers", required=True, help="Private JSON server configuration file")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", default=8787, type=int)
    parser.add_argument("--development", action="store_true", help="Enable local test accounts; requires MM_DEVELOPMENT_KEY")
    parser.add_argument("--trusted-proxy", action="append", default=[], metavar="IP",
                        help="Trust forwarding headers from this HTTPS proxy IP; repeat for multiple proxies. Disabled by default.")
    args = parser.parse_args()
    dev_key = os.environ.get("MM_DEVELOPMENT_KEY", "") if args.development else ""
    if args.development and (args.host not in {"127.0.0.1", "::1"} or len(dev_key) < 32):
        parser.error("Development mode requires a loopback bind and a 32+ character MM_DEVELOPMENT_KEY")
    if args.development and args.trusted_proxy:
        parser.error("Development mode cannot use trusted proxies")
    try:
        proxies = tuple(str(ipaddress.ip_address(address)) for address in args.trusted_proxy)
    except ValueError:
        parser.error("Each trusted proxy must be an explicit IPv4 or IPv6 address")
    settings = Settings(int(os.environ.get("MM_STEAM_APP_ID", "0")), os.environ.get("MM_STEAM_PUBLISHER_KEY", ""),
                        development_key=dev_key, trusted_proxies=proxies)
    Path(args.database).parent.mkdir(parents=True, exist_ok=True)
    core = Coordinator(args.database, json.loads(Path(args.servers).read_text(encoding="utf-8")))
    try:
        # The application handles only explicitly trusted proxies. Keep Uvicorn's
        # implicit/environment-configured proxy handling disabled in every mode.
        uvicorn.run(create_app(core, settings), host=args.host, port=args.port, proxy_headers=False)
    finally:
        core.close()


if __name__ == "__main__":
    main()

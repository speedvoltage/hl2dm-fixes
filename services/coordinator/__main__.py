import argparse
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
    args = parser.parse_args()
    dev_key = os.environ.get("MM_DEVELOPMENT_KEY", "") if args.development else ""
    if args.development and (args.host not in {"127.0.0.1", "::1"} or len(dev_key) < 32):
        parser.error("Development mode requires a loopback bind and a 32+ character MM_DEVELOPMENT_KEY")
    settings = Settings(int(os.environ.get("MM_STEAM_APP_ID", "0")), os.environ.get("MM_STEAM_PUBLISHER_KEY", ""), development_key=dev_key)
    Path(args.database).parent.mkdir(parents=True, exist_ok=True)
    core = Coordinator(args.database, json.loads(Path(args.servers).read_text(encoding="utf-8")))
    try:
        # No forwarded-IP trust: development mode must not be placed behind a proxy.
        uvicorn.run(create_app(core, settings), host=args.host, port=args.port, proxy_headers=False)
    finally:
        core.close()


if __name__ == "__main__":
    main()

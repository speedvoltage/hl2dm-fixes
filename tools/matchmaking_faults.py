"""Local fault injection shared by the native matchmaking runtime checks."""
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import threading

import httpx


class ServerFaultProxy:
    """Loopback-only fault injection for the native server's coordinator traffic."""
    def __init__(self, upstream):
        self.unavailable = threading.Event()
        self.forwarded = []
        owner = self

        class Handler(BaseHTTPRequestHandler):
            def log_message(self, *args):
                pass

            def do_POST(self):
                if not self.path.startswith("/v1/servers/"):
                    self.send_error(404)
                    return
                size = int(self.headers.get("Content-Length", "0"))
                if size < 0 or size > 65536:
                    self.send_error(413)
                    return
                body = self.rfile.read(size)
                if owner.unavailable.is_set():
                    code, payload = 503, b'{"error":"Injected coordinator outage"}'
                else:
                    response = httpx.post(upstream + self.path, content=body, headers={
                        "Authorization": self.headers.get("Authorization", ""),
                        "Content-Type": "application/json"}, timeout=5, trust_env=False)
                    owner.forwarded.append(self.path)
                    code, payload = response.status_code, response.content
                self.send_response(code)
                self.send_header("Content-Type", "application/json")
                self.send_header("Content-Length", str(len(payload)))
                self.end_headers()
                self.wfile.write(payload)

        self.server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.thread.start()
        self.url = f"http://127.0.0.1:{self.server.server_port}"

    def close(self):
        self.unavailable.clear()
        self.server.shutdown()
        self.server.server_close()
        self.thread.join(timeout=5)

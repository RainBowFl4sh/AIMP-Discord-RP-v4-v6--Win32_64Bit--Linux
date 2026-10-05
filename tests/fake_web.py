#!/usr/bin/env python3
"""Stand-in for the web services the plugin talks to, for the end-to-end tests (no internet needed).

Usage: fake_web.py <port file> <log file>
The plugin is started with AIMP_DISCORD_RPC_TEST_URL=http://127.0.0.1:<port>; it then requests
"https://api.github.com/x" as "http://127.0.0.1:<port>/api.github.com/x". Served:
  GitHub releases API  -> release 9.9.9 with an .aimppack asset (+ its sha256 digest)
  the .aimppack        -> a small ZIP with the plugin's folder name inside
  GitHub avatar, Discord application assets / avatars, Deezer search + cover -> small PNG pictures
  x0.at upload         -> https://x0.at/T3st.png (the file is served, like the real service, only to GET)
  files.catbox.moe     -> the uploaded picture; empty (0 bytes, like catbox.moe in October 2026) while the file
                          "<log file>.catbox-broken" exists
  catbox.moe upload    -> handed to PHP (tests/fake_catbox.php, "php -S") so the upload is parsed like on the real
                          service; without PHP a strict parser in Python stands in
Every request is logged as one line "<path>" to the log file.
"""
import email.parser
import email.policy
import hashlib
import io
import json
import os
import shutil
import signal
import socket
import struct
import subprocess
import sys
import time
import urllib.error
import urllib.request
import zipfile
import zlib
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

HERE = os.path.dirname(os.path.abspath(__file__))
PHP_PORT = None   # port of "php -S" serving fake_catbox.php
PHP = None


def start_php():
    global PHP_PORT, PHP
    if not shutil.which("php"):
        return
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    port = s.getsockname()[1]
    s.close()
    PHP = subprocess.Popen(["php", "-S", f"127.0.0.1:{port}", os.path.join(HERE, "fake_catbox.php")],
                     stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    for _ in range(50):
        try:
            socket.create_connection(("127.0.0.1", port), 0.2).close()
            PHP_PORT = port
            return
        except OSError:
            time.sleep(0.1)


def catbox(headers, body):
    """(status, answer) for an upload to catbox.moe/user/api.php"""
    if PHP_PORT:
        req = urllib.request.Request(f"http://127.0.0.1:{PHP_PORT}/user/api.php", body,
                                     {"Content-Type": headers.get("Content-Type", "")})
        with urllib.request.urlopen(req) as r:
            return r.status, r.read()
    msg = email.parser.BytesParser(policy=email.policy.HTTP).parsebytes(
        b"Content-Type: " + headers.get("Content-Type", "").encode() + b"\r\n\r\n" + body)
    parts = {p.get_param("name", header="content-disposition"): p for p in msg.iter_parts()} if msg.is_multipart() else {}
    f = parts.get("fileToUpload")
    if parts.get("reqtype") and parts["reqtype"].get_content().strip() == "fileupload" and f and f.get_filename() \
            and f.get_payload(decode=True):
        return 200, b"https://files.catbox.moe/t3st42." + f.get_filename().rsplit(".", 1)[-1].encode()
    return 200, b""

REPO = "RainBowFl4sh/AIMP-Discord-RP-v4-v6--Win32_64Bit--Linux"
APP = "1555109720807702559"


def png(rgb, size=32):
    raw = b"".join(b"\x00" + bytes(rgb) * size for _ in range(size))

    def chunk(t, b):
        return struct.pack(">I", len(b)) + t + b + struct.pack(">I", zlib.crc32(t + b) & 0xFFFFFFFF)

    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", size, size, 8, 2, 0, 0, 0)) +
            chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b""))


def package():
    buf = io.BytesIO()
    with zipfile.ZipFile(buf, "w", zipfile.ZIP_STORED) as z:
        z.writestr("aimp_discord_rpc/aimp_discord_rpc.txt", "Name: Discord Rich Presence\r\nVersion: 9.9.9\r\n" + "x" * 2000)
    return buf.getvalue()


PACKAGE = package()
if os.environ.get("FAKE_PACKAGE"):   # a real package (manual tests with a real AIMP)
    PACKAGE = open(os.environ["FAKE_PACKAGE"], "rb").read()
PACKAGE_URL = f"https://github.com/{REPO}/releases/download/v9.9.9/aimp_discord_rpc.aimppack"
RELEASE = {
    "url": f"https://api.github.com/repos/{REPO}/releases/1",
    "html_url": f"https://github.com/{REPO}/releases/tag/v9.9.9",
    "id": 1,
    "author": {"login": "RainBowFl4sh", "id": 66131975, "html_url": "https://github.com/RainBowFl4sh"},
    "tag_name": "v9.9.9",
    "name": "Test release",
    "draft": False,
    "prerelease": False,
    "assets": [   # decoys first: only the plugin's .aimppack may be used for the update
        {"name": "aimp_discord_rpc-windows-x64.zip", "uploader": {"login": "RainBowFl4sh"}, "size": 10,
         "browser_download_url": f"https://github.com/{REPO}/releases/download/v9.9.9/aimp_discord_rpc-windows-x64.zip"},
        {"name": "extras.aimppack", "uploader": {"login": "RainBowFl4sh"}, "size": 10,
         "browser_download_url": f"https://github.com/{REPO}/releases/download/v9.9.9/extras.aimppack"},
        {"name": "linux-x86_64.7z", "browser_download_url": f"https://github.com/{REPO}/releases/download/v9.9.9/linux-x86_64.7z",
         "uploader": {"login": "RainBowFl4sh"}, "size": 10, "digest": "sha256:00"},
        {"name": "aimp_discord_rpc.aimppack", "uploader": {"login": "RainBowFl4sh", "id": 66131975},
         "content_type": "application/octet-stream", "size": len(PACKAGE),
         "digest": "sha256:" + hashlib.sha256(PACKAGE).hexdigest(), "browser_download_url": PACKAGE_URL},
    ],
    "body": "## 9.9.9\r\n- test release",
}
ASSETS = [{"id": "1111", "type": 1, "name": "aimp"}, {"id": "2222", "type": 1, "name": "play"},
          {"id": "3333", "type": 1, "name": "pause"}, {"id": "4444", "type": 1, "name": "other"}]
DEEZER = {"data": [{"id": 1, "title": "A Night at the Opera",
                    "cover_xl": "https://e-cdns-images.dzcdn.net/images/cover/abc/1000x1000-000000-80-0-0.jpg"}],
          "total": 1}


class Handler(BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def reply(self, status, body, ctype="application/octet-stream"):
        if isinstance(body, (dict, list)):
            body, ctype = json.dumps(body).encode(), "application/json"
        self.send_response(status)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        path = self.path.split("?")[0]
        with open(self.server.log, "a") as f:
            f.write(self.path + "\n")
        if path == f"/api.github.com/repos/{REPO}/releases/latest":
            return self.reply(200, RELEASE)
        if path == PACKAGE_URL.replace("https:/", ""):
            return self.reply(200, PACKAGE)
        if path.startswith("/avatars.githubusercontent.com/"):
            return self.reply(200, png((60, 140, 220)), "image/png")
        if path == f"/discord.com/api/v9/oauth2/applications/{APP}/assets":
            return self.reply(200, ASSETS)
        if path.startswith("/discord.com/api/v9/applications/"):
            return self.reply(200, {"id": path.split("/")[5], "name": "My Test App"})
        if path.startswith("/cdn.discordapp.com/app-assets/"):
            colors = {"1111": (250, 160, 30), "2222": (240, 140, 20), "3333": (200, 120, 20)}
            return self.reply(200, png(colors.get(path.rsplit("/", 1)[1].split(".")[0], (90, 90, 90))), "image/png")
        if path.startswith("/cdn.discordapp.com/avatars/"):
            return self.reply(200, png((120, 80, 200)), "image/png")
        if path.startswith("/api.deezer.com/search/"):
            return self.reply(200, DEEZER)
        if path.startswith("/media.discordapp.net/external/"):   # Discord's media proxy
            if os.path.exists(self.server.log + ".proxy-broken") and "r=" not in self.path:
                return self.reply(415, b"", "text/plain")   # Discord could not load this URL
            return self.reply(200, png((20, 160, 90)), "image/png")
        if path.startswith("/files.catbox.moe/"):
            return self.reply(200, b"" if os.path.exists(self.server.log + ".catbox-broken") else png((10, 120, 200)),
                              "image/png")
        if path.startswith("/x0.at/") and len(path) > 7:
            return self.reply(200, png((20, 160, 90)), "image/png")
        if path.startswith("/e-cdns-images.dzcdn.net/"):
            return self.reply(200, png((180, 40, 60)), "image/png")
        self.reply(404, {"message": "Not Found"})

    def do_POST(self):
        if self.path.split("?")[0] == "/catbox.moe/user/api.php":
            body = self.rfile.read(int(self.headers.get("Content-Length", 0)))
            status, answer = catbox(self.headers, body)
            with open(self.server.log, "a") as f:
                f.write(f"{self.path} {len(body)} -> {answer.decode() or '(nothing)'}\n")
            return self.reply(status, answer, "text/html; charset=UTF-8")
        if self.path.split("?")[0] in ("/x0.at", "/x0.at/"):
            body = self.rfile.read(int(self.headers.get("Content-Length", 0)))
            ok = b'name="file"; filename="cover.' in body
            with open(self.server.log, "a") as f:
                f.write(f"{self.path} {len(body)} -> {'https://x0.at/T3st.png' if ok else '(nothing)'}\n")
            return self.reply(200 if ok else 400, b"https://x0.at/T3st.png\n" if ok else b"no file", "text/plain")
        self.do_GET()


def main():
    port_file, log = sys.argv[1], sys.argv[2]
    start_php()
    signal.signal(signal.SIGTERM, lambda *_: (PHP and PHP.kill(), os._exit(0)))   # the script ends us with kill
    srv = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    srv.log = log
    with open(port_file + ".tmp", "w") as f:
        f.write(str(srv.server_address[1]))
    os.replace(port_file + ".tmp", port_file)
    srv.serve_forever()


if __name__ == "__main__":
    main()

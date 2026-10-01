#!/usr/bin/env python3
"""Minimal stand-in for the Discord client's IPC socket (Linux / Unix socket).

Usage: fake_discord.py <socket path> <log file>
Answers the handshake with READY and logs every frame it receives as one JSON line: {"op": n, "data": {...}}.
"""
import json
import os
import socket
import struct
import sys
import threading


def serve(conn, log):
    def send(op, obj):
        data = json.dumps(obj).encode()
        conn.sendall(struct.pack("<II", op, len(data)) + data)

    def recv_exact(n):
        buf = b""
        while len(buf) < n:
            chunk = conn.recv(n - len(buf))
            if not chunk:
                raise EOFError
            buf += chunk
        return buf

    try:
        while True:
            op, length = struct.unpack("<II", recv_exact(8))
            payload = json.loads(recv_exact(length) or b"{}")
            with open(log, "a") as f:
                f.write(json.dumps({"op": op, "data": payload}) + "\n")
            if op == 0:  # handshake
                send(1, {"cmd": "DISPATCH", "evt": "READY",
                         "data": {"v": 1, "user": {"id": "1", "username": "tester"}}})
            elif op == 1:  # command
                send(1, {"cmd": payload.get("cmd"), "evt": None, "nonce": payload.get("nonce"), "data": {}})
            elif op == 3:  # ping
                send(4, payload)
    except (EOFError, ConnectionError):
        pass
    finally:
        conn.close()


def main():
    path, log = sys.argv[1], sys.argv[2]
    if os.path.exists(path):
        os.unlink(path)
    srv = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    srv.bind(path)
    srv.listen(4)
    print("listening on", path, flush=True)
    while True:
        conn, _ = srv.accept()
        threading.Thread(target=serve, args=(conn, log), daemon=True).start()


if __name__ == "__main__":
    main()

"""Bounded newline-JSON IPC over a private, same-UID Unix socket."""
from __future__ import annotations

import json
import logging
import os
import selectors
import socket
import stat
import struct
import time
from dataclasses import dataclass, field
from pathlib import Path
from typing import Callable

MAX_REQUEST = 64 * 1024
MAX_RESPONSE = 2 * 1024 * 1024
MAX_CLIENTS = 64


class IPCError(Exception):
    pass


def encode(data: dict) -> bytes:
    return (json.dumps(data, ensure_ascii=False, separators=(",", ":"), allow_nan=False) + "\n").encode()


def connect(path: Path, timeout: float = 3.0) -> socket.socket:
    client = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    client.settimeout(timeout)
    try:
        client.connect(str(path))
        if hasattr(socket, "SO_PEERCRED"):
            _pid, uid, _gid = struct.unpack("3i", client.getsockopt(socket.SOL_SOCKET, socket.SO_PEERCRED, 12))
            if uid != os.getuid():
                raise IPCError("声音服务套接字不属于当前用户")
    except (OSError, IPCError):
        client.close()
        raise
    return client


def request(path: Path, message: dict, timeout: float = 3.0) -> dict:
    try:
        with connect(path, timeout) as client:
            client.sendall(encode(message))
            response = bytearray()
            while b"\n" not in response:
                part = client.recv(65536)
                if not part:
                    raise IPCError("声音服务未返回完整响应")
                response.extend(part)
                if len(response) > MAX_RESPONSE:
                    raise IPCError("声音服务响应过大")
            data = json.loads(response.split(b"\n", 1)[0])
            if not isinstance(data, dict):
                raise IPCError("无效的声音服务响应")
            return data
    except (OSError, ValueError) as error:
        raise IPCError(f"无法连接声音服务：{error}") from error


@dataclass(eq=False)
class Client:
    socket: socket.socket
    incoming: bytearray = field(default_factory=bytearray)
    outgoing: bytearray = field(default_factory=bytearray)
    subscriber: bool = False
    done: bool = False
    opened: float = field(default_factory=time.monotonic)


class JsonServer:
    def __init__(self, path: Path, selector: selectors.BaseSelector,
                 handler: Callable[[dict], dict], state: Callable[[], dict]):
        self.path, self.selector, self.handler, self.state = path, selector, handler, state
        self.clients: set[Client] = set()
        if path.exists() or path.is_symlink():
            info = path.lstat()
            if info.st_uid != os.getuid() or not stat.S_ISSOCK(info.st_mode):
                raise IPCError(f"拒绝替换非本用户套接字：{path}")
            path.unlink()  # Only after the service singleton lock is held.
        self.socket = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.socket.bind(str(path))
        os.chmod(path, 0o600)
        self.socket.listen(32)
        self.socket.setblocking(False)
        selector.register(self.socket, selectors.EVENT_READ, self)

    def accept(self) -> None:
        try:
            connection, _address = self.socket.accept()
        except BlockingIOError:
            return
        if len(self.clients) >= MAX_CLIENTS:
            connection.close()
            return
        if hasattr(socket, "SO_PEERCRED"):
            _pid, uid, _gid = struct.unpack("3i", connection.getsockopt(socket.SOL_SOCKET, socket.SO_PEERCRED, 12))
            if uid != os.getuid():
                connection.close()
                return
        connection.setblocking(False)
        client = Client(connection)
        self.clients.add(client)
        self.selector.register(connection, selectors.EVENT_READ, client)

    def drop(self, client: Client) -> None:
        self.clients.discard(client)
        try:
            self.selector.unregister(client.socket)
        except (KeyError, ValueError):
            pass
        client.socket.close()

    def send(self, client: Client, data: dict) -> None:
        client.outgoing.extend(encode(data))
        if len(client.outgoing) > MAX_RESPONSE:
            self.drop(client)  # An unresponsive UI must not stall volume keys.
            return
        self.selector.modify(client.socket, selectors.EVENT_READ | selectors.EVENT_WRITE, client)

    def io(self, client: Client, mask: int) -> None:
        try:
            if mask & selectors.EVENT_READ:
                part = client.socket.recv(65536)
                if not part:
                    self.drop(client)
                    return
                if client.subscriber or client.done:
                    self.drop(client)  # One request per connection.
                    return
                client.incoming.extend(part)
                if len(client.incoming) > MAX_REQUEST:
                    client.done = True
                    self.send(client, {"ok": False, "error": "请求过大"})
                elif b"\n" in client.incoming:
                    client.done = True
                    try:
                        message = json.loads(client.incoming.split(b"\n", 1)[0])
                        if not isinstance(message, dict):
                            raise ValueError("请求必须是 JSON 对象")
                        if message.get("op") == "subscribe":
                            client.subscriber = True
                            self.send(client, {"event": "state", "state": self.state()})
                        else:
                            self.send(client, self.handler(message))
                    except (ValueError, UnicodeError) as error:
                        self.send(client, {"ok": False, "error": f"无效请求：{error}"})
                    except Exception:
                        logging.exception("Unexpected IPC handler error")
                        self.send(client, {"ok": False, "error": "声音服务内部错误，请查看 daemon.log"})
                    client.incoming.clear()
            if client not in self.clients:
                return
            if mask & selectors.EVENT_WRITE and client.outgoing:
                sent = client.socket.send(client.outgoing)
                del client.outgoing[:sent]
                if not client.outgoing:
                    if client.done and not client.subscriber:
                        self.drop(client)
                    else:
                        self.selector.modify(client.socket, selectors.EVENT_READ, client)
        except BlockingIOError:
            return
        except (OSError, ValueError):
            self.drop(client)

    def broadcast(self, state: dict) -> None:
        for client in tuple(self.clients):
            if client.subscriber:
                self.send(client, {"event": "state", "state": state})

    def expire(self) -> None:
        now = time.monotonic()
        for client in tuple(self.clients):
            if not client.subscriber and now - client.opened > 5.0:
                self.drop(client)

    def close(self) -> None:
        for client in tuple(self.clients):
            self.drop(client)
        self.selector.unregister(self.socket)
        self.socket.close()
        self.path.unlink(missing_ok=True)

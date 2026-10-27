"""Session service: serialized policy, audio/X11 events, IPC and one GTK panel."""
from __future__ import annotations

import fcntl
import logging
import os
import selectors
import signal
import socket
import subprocess
import sys
import time
from pathlib import Path

from .backend import PulseBackend, PulseEvents
from .engine import CommandError, Engine
from .ipc import Client, JsonServer
from .model import Target
from .notify import Notifications
from .storage import Paths, Preferences
from .x11 import X11Error, X11Monitor

ENTRY = Path(__file__).resolve().parents[1] / "audio.py"


def string(message: dict, key: str) -> str:
    value = message.get(key)
    if not isinstance(value, str) or not value or len(value) > 1024:
        raise CommandError(f"参数 {key} 必须是非空字符串")
    return value


def integer(message: dict, key: str, minimum: int, maximum: int) -> int:
    value = message.get(key)
    if type(value) is not int or not minimum <= value <= maximum:
        raise CommandError(f"参数 {key} 必须是 {minimum}–{maximum} 之间的整数")
    return value


class Service:
    def __init__(self, paths: Paths, engine: Engine, monitor, *, audio_events: bool = True):
        self.paths, self.engine, self.monitor = paths, engine, monitor
        self.selector = selectors.DefaultSelector()
        self.wake_read, self.wake_write = socket.socketpair()
        self.wake_read.setblocking(False)
        self.wake_write.setblocking(False)
        self.selector.register(self.wake_read, selectors.EVENT_READ, "wake")
        self.selector.register(monitor.fileno(), selectors.EVENT_READ, "focus")
        self.server = JsonServer(paths.socket, self.selector, self.handle, lambda: self.state)
        self.events = PulseEvents(self.wake) if audio_events else None
        self.running = True
        self.stop_at: float | None = None
        self.state: dict = {}
        self.revision = 0
        self.panel = {"open": False, "page": "apps", "choose": False, "generation": 0,
                      "anchor": [0, 0], "origin": "pointer"}
        self.panel_process: subprocess.Popen | None = None
        self.retired_panels: list[subprocess.Popen] = []
        self.panel_ready = False
        self.panel_started_at = 0.0
        self.panel_closed_at = 0.0

    def wake(self) -> None:
        try:
            self.wake_write.send(b"a")
        except (BlockingIOError, OSError):
            pass

    def stop(self) -> None:
        self.running = False
        self.wake()

    def publish(self) -> None:
        state = self.engine.view()
        state["panel"] = dict(self.panel)
        old = {k: v for k, v in self.state.items() if k != "revision"}
        if state != old:
            self.revision += 1
            state["revision"] = self.revision
            self.state = state
            self.server.broadcast(state)

    def _capture(self) -> None:
        if not self.panel["open"]:
            self.engine.set_window(self.monitor.current())

    def close_panel(self) -> None:
        self.panel["open"] = False
        self.panel["choose"] = False
        self.panel_closed_at = time.monotonic()
        self._capture()

    def open_panel(self, page: str, *, choose: bool = False, toggle: bool = True,
                   origin: str = "pointer") -> None:
        if origin not in ("pointer", "window"):
            raise CommandError("面板定位方式必须是 pointer / window")
        if self.panel["open"]:
            if toggle and not choose and self.panel["page"] == page:
                self.close_panel()
                return
            self.panel.update(page=page, choose=choose, generation=self.panel["generation"] + 1)
            return
        self._capture()  # Freeze the original context BEFORE GTK receives focus.
        if self.panel_process and self.panel_process.poll() is None:
            self.panel_process.terminate()
            self.retired_panels.append(self.panel_process)
        if origin == "window" and not self.engine.window.xid:
            origin = "pointer"  # An empty tag still has an operating monitor.
        anchor = self.engine.window.center if origin == "window" else self.monitor.pointer()
        self.panel.update(open=True, page=page, choose=choose, anchor=list(anchor), origin=origin,
                          generation=self.panel["generation"] + 1)
        self.panel_ready = False
        self.panel_started_at = time.monotonic()
        try:
            self.panel_process = subprocess.Popen([sys.executable, str(ENTRY), "panel-ui"],
                                                   stdin=subprocess.DEVNULL,
                                                   stdout=subprocess.DEVNULL, close_fds=True,
                                                   env=dict(os.environ, GDK_BACKEND="x11"))
        except OSError as error:
            self.close_panel()
            raise CommandError(f"无法启动声音面板：{error}") from error

    def _explicit_target(self, message: dict) -> Target:
        kind, key = message.get("kind"), message.get("key")
        if kind == "stream":
            index = integer(message, "key", 0, 2**32 - 1)
            return Target("stream", str(index), string(message, "app"))
        if kind in ("app", "sink"):
            key = string(message, "key")
            sink = self.engine.snapshot.sink_by_name(key) if kind == "sink" else None
            return Target(kind, key, sink.label if sink else key)
        raise CommandError("kind 必须是 app / stream / sink")

    def handle(self, message: dict) -> dict:
        op = message.get("op")
        result = "done"
        try:
            self._capture()
            if op in ("ping", "state", "status"):
                pass
            elif op == "stop":
                self.stop_at = time.monotonic() + 0.1
            elif op in ("adjust", "mute", "set-volume", "set-mute"):
                scope = message.get("scope", "smart")
                if scope not in ("smart", "default"):
                    raise CommandError("scope 必须是 smart / default")
                target = self._explicit_target(message) if op.startswith("set-") else None
                if op == "adjust":
                    result = self.engine.control("volume", scope, delta=integer(message, "delta", -100, 100))
                elif op == "set-volume":
                    result = self.engine.control("volume", value=integer(message, "value", 0, 100), target=target)
                elif op == "set-mute":
                    if type(message.get("value")) is not bool:
                        raise CommandError("静音状态必须是布尔值")
                    result = self.engine.control("mute", value=message["value"], target=target)
                else:
                    result = self.engine.control("mute", scope)
                if result == "choose":
                    self.engine.notify("选择要控制的应用", "多个后台应用正在播放；本次操作未改变音量或静音", None, "control")
                    self.open_panel("apps", choose=True, toggle=False, origin=message.get("origin", "window"))
            elif op == "panel":
                page = message.get("page", "apps")
                if page not in ("apps", "devices"):
                    raise CommandError("未知面板页面")
                self.open_panel(page, origin=message.get("origin", "pointer"))
            elif op == "panel-page":
                if message.get("page") not in ("apps", "devices"):
                    raise CommandError("未知面板页面")
                self.panel.update(page=message["page"], choose=False)
            elif op in ("panel-ready", "panel-closed"):
                if self.panel_process and message.get("pid") == self.panel_process.pid:
                    if op == "panel-ready":
                        self.panel_ready = True
                    else:
                        self.close_panel()
            elif op == "lock":
                self.engine.refresh()
                self.engine.set_lock(string(message, "key"))
                if self.panel["choose"]:
                    self.close_panel()
                self.engine.notify(f"已锁定 {self.engine.lock.name}", "切换窗口不会改变控制目标", None, "control")
            elif op == "unlock":
                self.engine.unlock()
                self.engine.notify("已解除声音目标锁定", "恢复按当前窗口与播放上下文选择目标", None, "control")
            elif op == "route":
                self.engine.route(string(message, "key"), string(message, "sink"))
            elif op == "forget":
                self.engine.forget(string(message, "key"))
            elif op == "default":
                self.engine.set_default(string(message, "sink"))
            else:
                raise CommandError("未知声音服务命令")
        except CommandError as error:
            self.engine.notify("声音操作未完成", str(error), None, "control")
            self.publish()
            return {"ok": False, "error": str(error), "state": self.state}
        self.publish()
        if op == "status":
            return {"ok": True, "bar": self.state["bar"], "revision": self.state["revision"]}
        return {"ok": True, "result": result, "state": self.state}

    def _check_panel(self) -> None:
        process = self.panel_process
        if process:
            code = process.poll()
            if code is not None:
                if self.panel["open"]:
                    self.close_panel()
                    if code != 0 or not self.panel_ready:
                        self.engine.notify("声音面板未能打开", "需要 GTK3 / PyGObject 和 X11；详细原因见 " + str(self.paths.log),
                                           None, "control")
                self.panel_process = None
            elif self.panel["open"] and not self.panel_ready and time.monotonic() - self.panel_started_at > 8.0:
                self.close_panel()
                process.terminate()
                self.engine.notify("声音面板启动超时", "请检查 GTK3 / X11 环境及 " + str(self.paths.log), None, "control")
            elif not self.panel["open"] and time.monotonic() - self.panel_closed_at > 1.0:
                process.terminate()
        self.retired_panels = [p for p in self.retired_panels if p.poll() is None]

    def run(self) -> None:
        try:
            self._capture()
            self.engine.refresh()
            self.publish()
            if self.events:
                self.events.start()
            audio_due = None
            retry_due = time.monotonic() + 2.0
            while self.running:
                now = time.monotonic()
                deadlines = [now + 0.5, retry_due]
                if audio_due is not None:
                    deadlines.append(audio_due)
                if self.stop_at is not None:
                    deadlines.append(self.stop_at)
                for key, mask in self.selector.select(max(0.0, min(deadlines) - now)):
                    if key.data is self.server:
                        self.server.accept()
                    elif isinstance(key.data, Client):
                        self.server.io(key.data, mask)
                    elif key.data == "wake":
                        while True:
                            try:
                                if not self.wake_read.recv(4096):
                                    break
                            except BlockingIOError:
                                break
                        if audio_due is None:
                            audio_due = time.monotonic() + 0.04
                    elif key.data == "focus":
                        if self.monitor.drain():
                            self._capture()
                # Xlib may have buffered focus events while doing a synchronous
                # property query; do not wait for another fd-readability event.
                if self.monitor.drain():
                    self._capture()
                now = time.monotonic()
                if audio_due is not None and now >= audio_due:
                    self.engine.refresh()
                    audio_due = None
                if now >= retry_due:
                    if not self.engine.connected or self.engine.routing_errors:
                        self.engine.refresh()
                    retry_due = now + 2.0
                self._check_panel()
                self.server.expire()
                self.publish()
                if self.stop_at is not None and now >= self.stop_at:
                    self.running = False
        except X11Error:
            logging.info("X11 session closed; dropping session-only target lock")
        finally:
            self.close()

    def close(self) -> None:
        if self.events:
            self.events.stop()
            if self.events.ident:
                self.events.join(timeout=3.0)
        for process in self.retired_panels + ([self.panel_process] if self.panel_process else []):
            if process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=1.0)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait(timeout=1.0)
        self.server.close()
        self.wake_read.close()
        self.wake_write.close()
        self.monitor.close()
        self.engine.backend.close()
        self.selector.close()


def run_daemon(paths: Paths) -> int:
    paths.prepare()
    descriptor = os.open(paths.lock, os.O_RDWR | os.O_CREAT | os.O_NOFOLLOW, 0o600)
    with os.fdopen(descriptor, "r+") as lock:
        try:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            return 0  # An already running session service owns this socket.
        lock.seek(0)
        lock.truncate()
        lock.write(str(os.getpid()) + "\n")
        lock.flush()
        backend = PulseBackend()
        try:
            monitor = X11Monitor()
        except Exception:
            backend.close()
            raise
        notifications = Notifications()
        notifications.start()
        engine = Engine(backend, Preferences(paths.preferences), notify=notifications.send)
        service = Service(paths, engine, monitor)
        previous = {}
        for signum in (signal.SIGTERM, signal.SIGINT, signal.SIGHUP):
            previous[signum] = signal.signal(signum, lambda _sig, _frame: service.stop())
        try:
            service.run()
        finally:
            notifications.stop()
            notifications.join(timeout=2.0)
            for signum, handler in previous.items():
                signal.signal(signum, handler)
    return 0

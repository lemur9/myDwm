"""Small CLI; status/IPC clients do not import GTK, Xlib or libpulse."""
from __future__ import annotations

import argparse
import importlib
import html
import json
import logging
import os
import shutil
import subprocess
import sys
import time
from pathlib import Path

from .backend import AudioError
from .ipc import IPCError, connect, encode, request
from .x11 import X11Error
from .storage import Paths, StorageError

ENTRY = Path(__file__).resolve().parents[1] / "audio.py"


def ensure_daemon(paths: Paths) -> None:
    paths.prepare()
    try:
        if request(paths.socket, {"op": "ping"}, timeout=0.3).get("ok"):
            return
    except IPCError:
        pass
    if paths.log.exists() and paths.log.stat().st_size > 1024 * 1024:
        os.replace(paths.log, paths.log.with_suffix(".log.1"))
    descriptor = os.open(paths.log, os.O_WRONLY | os.O_CREAT | os.O_APPEND | os.O_NOFOLLOW, 0o600)
    with os.fdopen(descriptor, "a") as output:
        process = subprocess.Popen([sys.executable, str(ENTRY), "daemon"],
                                   stdin=subprocess.DEVNULL, stdout=output, stderr=output,
                                   start_new_session=True, close_fds=True)
    deadline = time.monotonic() + 5.0
    while time.monotonic() < deadline:
        try:
            if request(paths.socket, {"op": "ping"}, timeout=0.4).get("ok"):
                return
        except IPCError:
            pass
        if process.poll() not in (None, 0):
            break
        time.sleep(0.06)
    try:
        last = paths.log.read_text(encoding="utf-8", errors="replace").splitlines()[-1]
    except (OSError, IndexError):
        last = "请执行 audioctl doctor 检查依赖与桌面环境"
    raise IPCError(f"无法启动声音服务：{last}（日志：{paths.log}）")


def startup_error_notice(message: str) -> None:
    # A failed daemon cannot send its own notification. dwm-launched commands
    # otherwise have no visible stderr; report missing dependencies there too.
    binary = shutil.which("notify-send")
    if binary:
        try:
            subprocess.run([binary, "--app-name=dwm-audio", "--replace-id=9527",
                            "--icon=dialog-error", "--expire-time=4000", "--",
                            "声音服务不可用", html.escape(message)], timeout=1.5,
                           stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        except (OSError, subprocess.SubprocessError):
            pass


def doctor() -> int:
    """Read-only diagnostic, never starts/restarts desktop or audio services."""
    failures = 0
    print(f"Python: {sys.executable} ({sys.version.split()[0]})")
    for name, label in (("pulsectl", "pulsectl / libpulse"), ("Xlib", "python-xlib")):
        try:
            importlib.import_module(name)
            print(f"[OK] {label}")
        except (ImportError, OSError) as error:
            print(f"[缺少] {label}: {error}")
            failures += 1
    try:
        import gi
        gi.require_version("Gtk", "3.0")
        from gi.repository import Gtk
        ready, _args = Gtk.init_check([])
        if not ready:
            raise RuntimeError("无法连接图形桌面")
        print("[OK] GTK3 / PyGObject / 图形桌面")
    except (ImportError, ValueError, RuntimeError) as error:
        print(f"[缺少] GTK3 / PyGObject / 图形桌面: {error}")
        failures += 1
    try:
        from .backend import PulseBackend
        backend = PulseBackend()
        try:
            snapshot = backend.snapshot()
            print(f"[OK] PulseAudio: {len(snapshot.sinks)} 个输出，{len(snapshot.streams)} 条流")
        finally:
            backend.close()
    except Exception as error:
        print(f"[未连接] PulseAudio: {error}")
        failures += 1
    print("[OK] notify-send" if shutil.which("notify-send") else "[可选缺少] notify-send（无法显示 Dunst 通知）")
    print(f"DISPLAY: {os.environ.get('DISPLAY', '(未设置)')}")
    paths = Paths.discover()
    print(f"会话套接字: {paths.socket}\n输出偏好: {paths.preferences}\n日志: {paths.log}")
    return 1 if failures else 0


def parser() -> argparse.ArgumentParser:
    result = argparse.ArgumentParser(prog="audioctl", description="dwm 智能声音控制（PulseAudio）")
    commands = result.add_subparsers(dest="command", required=True)
    for name, help_text in (("start", "启动唯一的会话服务"), ("daemon", "前台运行服务"),
                            ("stop", "停止本会话声音服务"), ("status", "输出状态栏文字，不自动启动服务"),
                            ("state", "输出共享 JSON 状态，不自动启动服务"), ("watch", "订阅 JSON 状态变化"),
                            ("doctor", "只读依赖与连接检查"), ("unlock", "解除应用锁定")):
        commands.add_parser(name, help=help_text)
    commands.add_parser("panel-ui", help="内部面板进程（由服务启动）")
    adjust = commands.add_parser("adjust", help="智能音量增减，单位：百分点")
    adjust.add_argument("delta", type=int, choices=range(-100, 101), metavar="-100..100")
    adjust.add_argument("--device", action="store_true", help="只控制默认输出设备")
    adjust.add_argument("--pointer", action="store_true", help="歧义面板在鼠标所在显示器打开")
    mute = commands.add_parser("mute", help="智能目标静音切换")
    mute.add_argument("--device", action="store_true")
    mute.add_argument("--pointer", action="store_true")
    panel = commands.add_parser("panel", help="打开/关闭原生声音浮窗")
    panel.add_argument("page", choices=("apps", "devices"), default="apps", nargs="?")
    lock = commands.add_parser("lock", help="锁定稳定应用身份（从 state 获取 key）")
    lock.add_argument("key")
    route = commands.add_parser("route", help="迁移应用并长期记住输出")
    route.add_argument("key")
    route.add_argument("sink")
    forget = commands.add_parser("forget", help="取消应用输出记忆")
    forget.add_argument("key")
    default = commands.add_parser("default", help="设置默认输出，不统一迁移应用")
    default.add_argument("sink")
    for name in ("set-volume", "set-mute"):
        control = commands.add_parser(name, help="控制面板中的显式目标")
        control.add_argument("value", type=int if name == "set-volume" else str,
                             choices=range(101) if name == "set-volume" else ("on", "off"),
                             metavar="0..100" if name == "set-volume" else "on|off")
        target = control.add_mutually_exclusive_group(required=True)
        target.add_argument("--app")
        target.add_argument("--sink")
        target.add_argument("--stream", type=int)
        control.add_argument("--owner", help="单流操作必须提供其稳定应用 key")
    return result


def main(argv: list[str] | None = None) -> int:
    args = parser().parse_args(argv)
    paths = Paths.discover()
    try:
        if args.command == "doctor":
            return doctor()
        if args.command == "daemon":
            from .service import run_daemon
            logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(message)s")
            return run_daemon(paths)
        if args.command == "panel-ui":
            from .panel import run_panel
            return run_panel(paths)
        if args.command == "start":
            ensure_daemon(paths)
            return 0
        if args.command == "status":
            try:
                data = request(paths.socket, {"op": "status"}, timeout=0.5)
                print(data.get("bar", " 音频离线"))
            except IPCError:
                print(" 音频离线")
            return 0
        if args.command in ("state", "stop"):
            try:
                data = request(paths.socket, {"op": args.command})
            except IPCError:
                if args.command == "stop":
                    return 0
                raise
            if args.command == "state":
                print(json.dumps(data["state"], ensure_ascii=False, indent=2))
            return 0 if data.get("ok") else 1
        ensure_daemon(paths)
        if args.command == "watch":
            with connect(paths.socket) as client:
                client.sendall(encode({"op": "subscribe"}))
                client.settimeout(None)
                with client.makefile("r", encoding="utf-8") as stream:
                    for line in stream:
                        print(line.rstrip(), flush=True)
            return 0
        message = {"op": args.command}
        if args.command in ("adjust", "mute"):
            message.update(scope="default" if args.device else "smart",
                           origin="pointer" if args.pointer else "window")
            if args.command == "adjust":
                message["delta"] = args.delta
        elif args.command == "panel":
            message["page"] = args.page
        elif args.command in ("lock", "route", "forget"):
            message["key"] = args.key
            if args.command == "route":
                message["sink"] = args.sink
        elif args.command == "default":
            message["sink"] = args.sink
        elif args.command in ("set-volume", "set-mute"):
            message["value"] = args.value if args.command == "set-volume" else args.value == "on"
            if args.app:
                message.update(kind="app", key=args.app)
            elif args.sink:
                message.update(kind="sink", key=args.sink)
            else:
                if not args.owner:
                    raise IPCError("--stream 必须同时提供 --owner 应用key")
                message.update(kind="stream", key=args.stream, app=args.owner)
        data = request(paths.socket, message)
        if not data.get("ok"):
            print(data.get("error", "声音操作失败"), file=sys.stderr)
            return 1
        return 0
    except (IPCError, StorageError, AudioError, X11Error, OSError, ImportError, RuntimeError, ValueError) as error:
        print(f"audioctl: {error}", file=sys.stderr)
        if args.command not in ("daemon", "panel-ui", "state", "status", "doctor", "stop"):
            startup_error_notice(str(error))
        return 1
    except KeyboardInterrupt:
        return 130

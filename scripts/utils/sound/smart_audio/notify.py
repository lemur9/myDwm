"""Nonblocking, coalesced Dunst/libnotify notifications (optional dependency)."""
from __future__ import annotations

import hashlib
import html
import logging
import shutil
import subprocess
import threading
from collections import OrderedDict


class Notifications(threading.Thread):
    def __init__(self):
        super().__init__(name="audio-notifications", daemon=True)
        self.binary = shutil.which("notify-send")
        self.pending: OrderedDict[str, tuple] = OrderedDict()
        self.condition = threading.Condition()
        self.stopped = False

    def send(self, summary: str, body: str = "", value: int | None = None, tag: str = "control") -> None:
        if not self.binary:
            return
        with self.condition:
            self.pending[tag] = (summary, body, value)
            if len(self.pending) > 32:
                self.pending.popitem(last=False)
            self.condition.notify()

    def stop(self) -> None:
        with self.condition:
            self.stopped = True
            self.condition.notify()

    def run(self) -> None:
        while True:
            with self.condition:
                self.condition.wait_for(lambda: self.pending or self.stopped)
                if self.stopped:
                    return
                tag, (summary, body, value) = self.pending.popitem(last=False)
            token = hashlib.sha256(tag.encode()).hexdigest()[:12]
            args = [self.binary, "--app-name=dwm-audio", "--icon=audio-volume-high",
                    "--expire-time=2200", "--hint=string:x-dunst-stack-tag:dwm-audio-" + token,
                    "--hint=string:hlcolor:#89b4fa"]
            if tag == "control":
                args += ["--replace-id=9527"]
            if value is not None and 0 <= value <= 100:
                args += [f"--hint=int:value:{value}"]
            args += ["--", html.escape(summary), html.escape(body)]
            try:
                subprocess.run(args, check=True, timeout=1.5, stdin=subprocess.DEVNULL,
                               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            except (OSError, subprocess.SubprocessError):
                logging.debug("Notification delivery failed", exc_info=True)

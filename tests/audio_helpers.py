"""Headless fixtures. No X11, GTK or PulseAudio dependency is imported."""
from __future__ import annotations

import socket
import sys
from dataclasses import replace
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts/utils/sound"))

from smart_audio.backend import AudioError, AudioUnavailable
from smart_audio.identity import Process, Processes
from smart_audio.model import Sink, Snapshot, Stream, Window

SPEAKERS = Sink(1, "alsa.speakers", "内置扬声器")
USB = Sink(2, "alsa.usb", "USB 耳机")
BLUETOOTH = Sink(3, "bluez.headset.a2dp_sink", "蓝牙耳机")


def stream(index=10, binary="mpv", pid=100, sink=1, volume=50, **kwargs):
    properties = {"application.name": binary, "application.process.binary": binary,
                  "application.process.id": str(pid), "media.name": "示例媒体"}
    properties.update(kwargs.pop("properties", {}))
    return Stream(index, sink, properties, (volume / 100, volume / 100), **kwargs)


class FakeProcesses(Processes):
    def __init__(self, *rows: Process):
        super().__init__()
        self.table = {row.pid: row for row in rows}

    def get(self, pid):
        return self.table.get(pid)


class FakeBackend:
    def __init__(self, data=Snapshot((SPEAKERS, USB, BLUETOOTH), (), SPEAKERS.name)):
        self.data = data
        self.calls = []
        self.online = True
        self.fail_moves = set()
        self.fail_volume = set()
        self.no_op = False
        self.closed = False

    def snapshot(self):
        if not self.online:
            raise AudioUnavailable("测试音频服务断开")
        return self.data

    def _update(self, kind, index, **kwargs):
        name = "sinks" if kind == "sink" else "streams"
        items = getattr(self.data, name)
        if not any(item.index == index for item in items):
            raise AudioError("目标已退出")
        if not self.no_op:
            self.data = replace(self.data, **{name: tuple(replace(item, **kwargs) if item.index == index else item
                                                        for item in items)})

    def set_volume(self, kind, index, value):
        self.calls.append(("volume", kind, index, value))
        if index in self.fail_volume:
            raise AudioError("测试音量写入失败")
        self._update(kind, index, channels=(value / 100, value / 100))

    def set_mute(self, kind, index, value):
        self.calls.append(("mute", kind, index, value))
        self._update(kind, index, muted=value)

    def move(self, index, sink_index):
        self.calls.append(("move", index, sink_index))
        if index in self.fail_moves:
            raise AudioError("测试迁移失败")
        self._update("stream", index, sink=sink_index)

    def set_default(self, name):
        self.calls.append(("default", name))
        if not self.no_op:
            self.data = replace(self.data, default_sink=name)

    def forget_restore_devices(self, streams):
        self.calls.append(("forget-restore", tuple(s.index for s in streams)))

    def close(self):
        self.closed = True


class FakeMonitor:
    def __init__(self, window=Window()):
        self.window = window
        self.reader, self.writer = socket.socketpair()
        self.reader.setblocking(False)

    def fileno(self):
        return self.reader.fileno()

    def current(self):
        return self.window

    def pointer(self):
        return (1400, 30)

    def set(self, window):
        self.window = window
        self.writer.send(b"f")

    def drain(self):
        changed = False
        while True:
            try:
                if not self.reader.recv(4096):
                    break
                changed = True
            except BlockingIOError:
                break
        return changed

    def close(self):
        self.reader.close()
        self.writer.close()

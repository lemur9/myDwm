"""Structured libpulse access. No pactl output parsing or backend migration.

The event connection belongs exclusively to its worker thread. All reads and
writes use a different connection on the service thread, never a pulse callback.
"""
from __future__ import annotations

import logging
import threading
from typing import Callable

from .identity import clean_text
from .model import Sink, Snapshot, Stream, bounded


class AudioError(Exception):
    pass


class AudioUnavailable(AudioError):
    pass


def load_pulse():
    try:
        import pulsectl
    except (ImportError, OSError) as error:
        raise AudioUnavailable("需要 python-pulsectl 和 libpulse；请参见 docs/smart-audio.md") from error
    # pulsectl 24.x does not copy these two public pa_sink_input_info fields by
    # default. Include them in its normal struct decoder, without parsing text
    # or depending on private ctypes layouts.
    cls = pulsectl.PulseSinkInputInfo
    fields = cls.c_struct_fields.split() if isinstance(cls.c_struct_fields, str) else list(cls.c_struct_fields)
    cls.c_struct_fields = fields + [f for f in ("has_volume", "volume_writable") if f not in fields]
    return pulsectl


def enum_name(value) -> str:
    # pulsectl.EnumValue compares to strings but repr/str is not its value.
    return getattr(value, "_value", str(value)).casefold()


def sink_from_pulse(item) -> Sink:
    port = getattr(item, "port_active", None)
    available = not (port and enum_name(port.available) == "no")
    state = enum_name(item.state)
    return Sink(item.index, item.name, clean_text(item.description or item.name),
                tuple(item.volume.values), bool(item.mute),
                bool(available and state != "invalid"), state)


def stream_from_pulse(item) -> Stream:
    return Stream(item.index, item.sink, dict(item.proplist), tuple(item.volume.values),
                  bool(item.mute), bool(item.corked),
                  bool(getattr(item, "has_volume", True) and getattr(item, "volume_writable", True)),
                  int(item.sample_spec.rate))


class PulseBackend:
    def __init__(self):
        self.module = load_pulse()
        self.pulse = None

    def close(self) -> None:
        if self.pulse:
            self.pulse.close()
            self.pulse = None

    def _connect(self):
        if self.pulse is None:
            pulse = self.module.Pulse("dwm-smart-audio", connect=False)
            try:
                # Do not start, stop, or replace the user's audio server.
                pulse.connect(autospawn=False, timeout=1.5)
            except Exception:
                pulse.close()
                raise
            self.pulse = pulse
        return self.pulse

    def _call(self, callback):
        try:
            return callback(self._connect())
        except (self.module.PulseError, self.module.PulseDisconnected, OSError) as error:
            disconnected = self.pulse is None or not self.pulse.connected
            if disconnected:
                self.close()
                raise AudioUnavailable("PulseAudio 连接中断，正在等待音频服务恢复") from error
            logging.debug("PulseAudio rejected an operation: %s", error, exc_info=True)
            raise AudioError("音频服务拒绝操作；应用可能已退出或设备状态已改变") from error

    def snapshot(self) -> Snapshot:
        def read(pulse):
            default = pulse.server_info().default_sink_name
            return Snapshot(tuple(map(sink_from_pulse, pulse.sink_list())),
                            tuple(map(stream_from_pulse, pulse.sink_input_list())), default)
        return self._call(read)

    def set_volume(self, kind: str, index: int, value: int) -> None:
        def write(pulse):
            obj = pulse.sink_info(index) if kind == "sink" else pulse.sink_input_info(index)
            pulse.volume_set_all_chans(obj, bounded(value) / 100.0)
        self._call(write)

    def set_mute(self, kind: str, index: int, muted: bool) -> None:
        self._call(lambda p: (p.sink_mute if kind == "sink" else p.sink_input_mute)(index, muted))

    def move(self, index: int, sink_index: int) -> None:
        self._call(lambda p: p.sink_input_move(index, sink_index))

    def set_default(self, name: str) -> None:
        self._call(lambda p: p.sink_default_set(name))

    def forget_restore_devices(self, streams: list[Stream]) -> None:
        """Forget only these apps' libpulse stream-restore device overrides.

        PulseAudio's own module-stream-restore can otherwise keep routing a
        previously moved app to the old device after our preference is removed.
        Preserve its remembered volume/mute/channel map and other apps' records.
        No entry removal and no moving unrelated streams.
        """
        names = {s.properties.get("module-stream-restore.id") for s in streams}
        names.discard(None)
        if not names:
            return

        def forget(pulse):
            for entry in pulse.stream_restore_list():
                if entry.name in names:
                    entry.device = None
                    pulse.stream_restore_write(entry, mode="replace", apply_immediately=False)
        self._call(forget)


class PulseEvents(threading.Thread):
    def __init__(self, changed: Callable[[], None]):
        super().__init__(name="audio-events", daemon=True)
        self.changed = changed
        self.stopped = threading.Event()

    def stop(self) -> None:
        self.stopped.set()

    def run(self) -> None:
        module = load_pulse()
        delay = 1.0
        while not self.stopped.is_set():
            pulse = None
            try:
                pulse = module.Pulse("dwm-smart-audio-events", connect=False)
                pulse.connect(autospawn=False, timeout=1.5)
                pulse.event_mask_set("sink", "sink_input", "server", "card")
                pulse.event_callback_set(lambda _event: self.changed())
                self.changed()
                delay = 1.0
                while not self.stopped.is_set():
                    pulse.event_listen(timeout=1.0)
            except (module.PulseError, module.PulseDisconnected, OSError):
                logging.debug("Audio event connection lost; reconnecting", exc_info=True)
                self.changed()
            finally:
                if pulse:
                    pulse.close()
            if self.stopped.wait(delay):
                break
            delay = min(10.0, delay * 2)

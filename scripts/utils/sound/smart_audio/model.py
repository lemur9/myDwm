"""Backend-independent snapshots, grouping, target policy and presentation."""
from __future__ import annotations

from dataclasses import dataclass, field
from typing import Iterable

from .identity import Identity, Processes, clean_text, family, identify


def percent(channels: tuple[float, ...]) -> int:
    return round(100 * sum(channels) / len(channels)) if channels else 0


def bounded(value: float) -> int:
    return max(0, min(100, round(value)))


@dataclass(frozen=True)
class Sink:
    index: int
    name: str                         # Stable PulseAudio sink name, NOT its index.
    label: str
    channels: tuple[float, ...] = (0.5, 0.5)
    muted: bool = False
    available: bool = True
    state: str = "idle"               # suspended is still available.

    @property
    def volume(self) -> int:
        return percent(self.channels)


@dataclass(frozen=True)
class Stream:
    index: int
    sink: int
    properties: dict[str, str] = field(default_factory=dict)
    channels: tuple[float, ...] = (0.5, 0.5)
    muted: bool = False
    corked: bool = False
    writable: bool = True
    rate: int = 0

    @property
    def pid(self) -> int:
        try:
            return max(0, int(self.properties.get("application.process.id", "0")))
        except ValueError:
            return 0

    @property
    def volume(self) -> int:
        return percent(self.channels)

    @property
    def media(self) -> str:
        return clean_text(self.properties.get("media.name", ""))


@dataclass(frozen=True)
class Snapshot:
    sinks: tuple[Sink, ...] = ()
    streams: tuple[Stream, ...] = ()
    default_sink: str = ""

    def sink_by_name(self, name: str) -> Sink | None:
        return next((s for s in self.sinks if s.name == name), None)

    def sink_by_index(self, index: int) -> Sink | None:
        return next((s for s in self.sinks if s.index == index), None)

    def fallback(self) -> Sink | None:
        default = self.sink_by_name(self.default_sink)
        if default and default.available:
            return default
        return next(iter(sorted((s for s in self.sinks if s.available),
                                key=lambda s: s.name)), None)


@dataclass(frozen=True)
class Window:
    xid: int = 0
    pid: int = 0
    classes: tuple[str, ...] = ()
    center: tuple[int, int] = (0, 0)

    @property
    def is_panel(self) -> bool:
        return any(c.casefold() in ("dwmaudio", "dwm-audio") for c in self.classes)


@dataclass
class App:
    identity: Identity
    streams: list[Stream] = field(default_factory=list)

    @property
    def key(self) -> str:
        return self.identity.key

    @property
    def controllable(self) -> bool:
        return any(s.writable for s in self.streams)

    @property
    def playing(self) -> bool:
        return any(not s.corked for s in self.streams)


def group_apps(snapshot: Snapshot, processes: Processes) -> dict[str, App]:
    apps: dict[str, App] = {}
    for stream in sorted(snapshot.streams, key=lambda s: s.index):
        identity = identify(stream.properties, stream.pid, processes)
        app = apps.setdefault(identity.key, App(identity))
        app.streams.append(stream)
    return apps


def foreground_app(apps: dict[str, App], window: Window, processes: Processes) -> str | None:
    scores: dict[str, int] = {}
    for app in apps.values():
        if not app.controllable:
            continue
        score = max((processes.related(window.pid, s.pid, app.identity.family)
                     for s in app.streams), default=0)
        aliases = {app.identity.family, family(app.identity.name)}
        if any(family(c) in aliases for c in window.classes if c):
            score = max(score, 60)
        if score:
            scores[app.key] = score
    if not scores:
        return None
    best = max(scores.values())
    winners = [key for key, score in scores.items() if score == best]
    return winners[0] if len(winners) == 1 else None


@dataclass(frozen=True)
class Target:
    kind: str                        # app / sink / ambiguous / unavailable
    key: str = ""
    name: str = ""
    locked: bool = False
    reason: str = ""


def select_target(snapshot: Snapshot, apps: dict[str, App], foreground: str | None,
                  lock: Identity | None) -> Target:
    if lock:
        app = apps.get(lock.key)
        if app and app.controllable:
            return Target("app", app.key, app.identity.name, True)
        return Target("unavailable", lock.key, lock.name, True, "锁定应用暂无可控制的音频流")
    if foreground and foreground in apps and apps[foreground].controllable:
        return Target("app", foreground, apps[foreground].identity.name)
    playing = [a for a in apps.values() if a.controllable and a.playing]
    if len(playing) == 1:
        app = playing[0]
        return Target("app", app.key, app.identity.name)
    if len(playing) > 1:
        return Target("ambiguous", name="选择应用", reason="多个后台应用正在播放，请选择并锁定")
    default = snapshot.sink_by_name(snapshot.default_sink)
    if default and default.available:
        return Target("sink", default.name, default.label)
    return Target("unavailable", name="等待输出设备", reason="没有可用的默认输出设备")


def levels(items: Iterable[Stream | Sink]) -> dict:
    items = list(items)
    volumes = [i.volume for i in items]
    mutes = {i.muted for i in items}
    channel_values = [round(v * 100) for i in items for v in i.channels]
    low, high = min(volumes, default=0), max(volumes, default=0)
    return {
        "volume": round(sum(volumes) / len(volumes)) if volumes else 0,
        "minimum": low, "maximum": high,
        "mixed_volume": low != high,
        "channel_mixed": len(set(channel_values)) > 1,
        "muted": next(iter(mutes)) if len(mutes) == 1 else None,
        "text": f"{low}%" if low == high else f"{low}–{high}%",
    }


def level_text(level: dict) -> str:
    mute = level["muted"]
    return level["text"] + (" · 已静音" if mute is True else
                            " · 部分静音" if mute is None else "")


def bar_text(target: Target, level: dict | None, connected: bool) -> str:
    name = clean_text(target.name, 12)
    if len(target.name) > 12:
        name += "…"
    lock = "󰌾 " if target.locked else ""
    if not connected:
        return f" {lock}{name or '音频'} 离线"
    if not level:
        return f" {lock}{name} --" if target.kind != "ambiguous" else " 选择应用"
    muted, volume = level["muted"], level["volume"]
    icon = "" if muted is True else "" if volume == 0 else "" if volume < 35 else ""
    return f"{icon} {lock}{name} {level_text(level)}"


def panel_geometry(area: tuple[int, int, int, int], anchor: tuple[int, int], origin: str,
                   size: tuple[int, int] = (620, 650)) -> tuple[int, int, int, int]:
    """Clamp a popup to a monitor workarea, including negative monitor origins."""
    ax, ay, aw, ah = area
    margin = min(12, max(0, (min(aw, ah) - 1) // 2))
    width, height = min(size[0], max(1, aw - 2 * margin)), min(size[1], max(1, ah - 2 * margin))
    x, y = anchor
    left = x - width + 28 if origin == "pointer" else x - width // 2
    top = y + 20 if origin == "pointer" else y - height // 2
    return (max(ax + margin, min(left, ax + aw - width - margin)),
            max(ay + margin, min(top, ay + ah - height - margin)), width, height)

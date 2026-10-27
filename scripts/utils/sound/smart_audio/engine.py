"""Single-owner audio policy, routing reconciliation, and verified operations."""
from __future__ import annotations

from dataclasses import asdict
from typing import Callable

from .backend import AudioError
from .identity import Identity, Processes, clean_text
from .model import (App, Sink, Snapshot, Stream, Target, Window, bar_text, bounded,
                    foreground_app, group_apps, level_text, levels, select_target)
from .storage import Preferences, StorageError


class CommandError(Exception):
    pass


class Engine:
    def __init__(self, backend, preferences: Preferences, processes: Processes | None = None,
                 notify: Callable = lambda *_args: None):
        self.backend = backend
        self.preferences = preferences
        self.processes = processes or Processes()
        self.notify = notify
        self.snapshot = Snapshot()
        self.apps: dict[str, App] = {}
        self.window = Window()
        self.lock: Identity | None = None  # Never serialized; not a stream index.
        self.connected = False
        self.error = ""
        self.routing_errors: dict[str, str] = {}
        self._routes: dict[str, tuple[str, str, str]] = {}
        self._reported_errors: dict[str, str] = {}

    def _read(self) -> bool:
        try:
            self.snapshot = self.backend.snapshot()
            self.connected, self.error = True, ""
        except AudioError as error:
            self.connected, self.error = False, str(error)
            self.snapshot = Snapshot()  # Never present stale streams as controllable.
        self.processes.clear()
        self.apps = group_apps(self.snapshot, self.processes)
        return self.connected

    def refresh(self, reconcile: bool = True) -> None:
        if self._read() and reconcile:
            self._reconcile_routes()

    @property
    def foreground(self) -> str | None:
        return foreground_app(self.apps, self.window, self.processes)

    @property
    def target(self) -> Target:
        return select_target(self.snapshot, self.apps, self.foreground, self.lock)

    def _output(self, streams: list[Stream]) -> tuple[list[dict], str]:
        outputs: dict[int, dict] = {}
        for stream in streams:
            sink = self.snapshot.sink_by_index(stream.sink)
            outputs[stream.sink] = {"name": sink.name if sink else "",
                                    "label": sink.label if sink else "等待输出设备",
                                    "available": bool(sink and sink.available)}
        values = list(outputs.values())
        return values, " / ".join(s["label"] for s in values) or "等待输出设备"

    def _target_items(self, target: Target) -> list[Sink | Stream]:
        if target.kind == "app":
            app = self.apps.get(target.key)
            if app and app.controllable:
                return list(app.streams)
        elif target.kind == "sink":
            sink = self.snapshot.sink_by_name(target.key)
            if sink and sink.available:
                return [sink]
        elif target.kind == "stream":
            # key is the index, name is the owning stable app key. Both are
            # required so a stale expanded row cannot operate on another app.
            app = self.apps.get(target.name)
            if app:
                return [s for s in app.streams if str(s.index) == target.key and s.writable]
        raise CommandError(target.reason or "控制目标已不可用，请重新选择")

    def _target_view(self, target: Target) -> dict:
        data = asdict(target)
        data["level"] = None
        data["output"] = ""
        if target.kind in ("app", "sink"):
            items = self._target_items(target)
            data["level"] = levels(items)
            data["output"] = (self._output(items)[1] if target.kind == "app" else target.name)
        return data

    def view(self) -> dict:
        foreground, target = self.foreground, self.target
        target_view = self._target_view(target)
        app_views = []
        for app in self.apps.values():
            outputs, output_label = self._output(app.streams)
            streams = []
            for stream in app.streams:
                sink = self.snapshot.sink_by_index(stream.sink)
                streams.append({"index": stream.index, "pid": stream.pid, "media": stream.media,
                                "rate": stream.rate, "channels": len(stream.channels),
                                "channel_values": [round(v * 100) for v in stream.channels],
                                "playing": not stream.corked, "writable": stream.writable,
                                "output": sink.label if sink else "等待输出设备",
                                "level": levels([stream])})
            preference = self.preferences.apps.get(app.key)
            app_views.append({"key": app.key, "name": app.identity.name, "icon": app.identity.icon,
                              "playing": app.playing, "writable": app.controllable,
                              "locked": bool(self.lock and self.lock.key == app.key),
                              "is_target": target.kind == "app" and target.key == app.key,
                              "is_foreground": app.key == foreground,
                              "level": levels(app.streams), "streams": streams,
                              "outputs": outputs, "output": output_label,
                              "preference": dict(preference) if preference else None,
                              "route_error": self.routing_errors.get(app.key, "")})
        app_views.sort(key=lambda a: (not a["is_target"], not a["is_foreground"],
                                     not a["playing"], a["name"].casefold(), a["key"]))
        sinks = [{"name": s.name, "label": s.label, "available": s.available,
                  "state": s.state, "default": s.name == self.snapshot.default_sink,
                  "level": levels([s])} for s in sorted(self.snapshot.sinks, key=lambda s: s.name)]
        return {"connected": self.connected,
                "error": self.error or self.preferences.error,
                "waiting_for_output": not any(s.available for s in self.snapshot.sinks),
                "lock": asdict(self.lock) if self.lock else None,
                "foreground": foreground, "target": target_view, "apps": app_views, "sinks": sinks,
                "bar": bar_text(target, target_view["level"], self.connected)}

    def _reconcile_routes(self) -> None:
        fallback = self.snapshot.fallback()
        plans: dict[str, tuple[str, str, str]] = {}
        errors: dict[str, str] = {}
        planned_streams: dict[str, set[int]] = {}
        originals: dict[str, str] = {}
        attempted = False
        for app in self.apps.values():
            preference = self.preferences.apps.get(app.key)
            preferred = self.snapshot.sink_by_name(preference["sink"]) if preference else None
            destination = preferred if preferred and preferred.available else fallback
            affected = [s for s in app.streams
                        if not (self.snapshot.sink_by_index(s.sink) and
                                self.snapshot.sink_by_index(s.sink).available)]
            if not preference and not affected:
                self._routes.pop(app.key, None)
                continue  # Changing default never sweeps existing apps onto it.
            mode = "preferred" if preferred and preferred.available else "fallback" if destination else "waiting"
            plans[app.key] = (preference["sink"] if preference else "", destination.name if destination else "", mode)
            if not destination:
                continue
            streams = app.streams if preference else affected
            planned_streams[app.key] = {s.index for s in streams}
            old_sink = self.snapshot.sink_by_index(streams[0].sink) if streams else None
            originals[app.key] = old_sink.label if old_sink else "原输出设备"
            for stream in streams:
                if stream.sink == destination.index:
                    continue
                attempted = True
                try:
                    self.backend.move(stream.index, destination.index)
                except AudioError as error:
                    errors[app.key] = str(error)
        # A successful libpulse return is not sufficient: read the resulting
        # routes before emitting either a success notification or shared state.
        if attempted and not self._read():
            return
        for key, plan in plans.items():
            app = self.apps.get(key)
            if not app:
                continue
            wanted, destination_name, mode = plan
            destination = self.snapshot.sink_by_name(destination_name)
            if mode != "waiting" and (not destination or not destination.available):
                errors.setdefault(key, "输出设备在切换过程中变为不可用")
            # An unpreferred app may deliberately use several valid outputs;
            # only its disconnected streams were part of this fallback plan.
            checked = app.streams if wanted else [s for s in app.streams if s.index in planned_streams.get(key, set())]
            if destination and any(s.sink != destination.index for s in checked):
                errors.setdefault(key, "部分音频流未能迁移到所选输出设备")
            if key in errors:
                continue
            old = self._routes.get(key)
            preference = self.preferences.apps.get(key, {})
            original = clean_text(preference.get("sink_label") or wanted or originals.get(key, "原输出设备"))
            if mode == "fallback" and old != plan:
                self.notify(f"{app.identity.name} · {original}不可用，已切换到 {destination.label}",
                            "原输出偏好仍保留" if wanted else "使用可用备用输出", None, "route:" + key)
            elif mode == "preferred" and old and old[0] == wanted and old[2] in ("fallback", "waiting"):
                self.notify(f"{app.identity.name} · {destination.label}恢复，已切回", "", None, "route:" + key)
            self._routes[key] = plan
        self.routing_errors = errors
        for key, message in errors.items():
            if self._reported_errors.get(key) != message:
                name = self.apps[key].identity.name if key in self.apps else key
                self.notify(f"{name} · 输出切换失败", message, None, "route:" + key)
        self._reported_errors = dict(errors)

    def set_window(self, window: Window) -> None:
        if not window.is_panel:
            self.window = window
            self.processes.clear()

    def set_lock(self, key: str) -> None:
        app = self.apps.get(key)
        if not app or not app.controllable:
            raise CommandError("该应用暂无可控制的音频流")
        self.lock = app.identity

    def unlock(self) -> None:
        self.lock = None

    def control(self, operation: str, scope: str = "smart", *, delta: int | None = None,
                value: int | bool | None = None, target: Target | None = None) -> str:
        self.refresh()
        if not self.connected:
            raise CommandError(self.error)
        if target is None:
            if scope == "default":
                sink = self.snapshot.sink_by_name(self.snapshot.default_sink)
                if not sink or not sink.available:
                    raise CommandError("等待可用的默认输出设备")
                target = Target("sink", sink.name, sink.label)
            else:
                target = self.target
        if target.kind == "ambiguous":
            return "choose"  # No saved delta, deferred mute, or replay after selection.
        items = self._target_items(target)
        desired = {}
        errors = []
        muted = not all(item.muted for item in items) if value is None else bool(value)
        for item in items:
            if operation == "volume" and isinstance(item, Stream) and not item.writable:
                continue
            kind = "sink" if isinstance(item, Sink) else "stream"
            try:
                if operation == "volume":
                    wanted = bounded(item.volume + delta if delta is not None else value)
                    self.backend.set_volume(kind, item.index, wanted)
                elif operation == "mute":
                    wanted = muted
                    self.backend.set_mute(kind, item.index, wanted)
                else:
                    raise CommandError("未知音频操作")
                desired[(kind, item.index)] = wanted
            except AudioError as error:
                errors.append(str(error))
        self.refresh(reconcile=False)
        if not self.connected:
            raise CommandError(self.error)
        current = {("sink", s.index): s for s in self.snapshot.sinks}
        current.update({("stream", s.index): s for s in self.snapshot.streams})
        for key, wanted in desired.items():
            item = current.get(key)
            if item is None or (item.volume if operation == "volume" else item.muted) != wanted:
                errors.append("目标已消失或音频状态未按请求更新")
        if errors:
            raise CommandError("；".join(dict.fromkeys(errors)))
        actual = self._target_items(target)
        level = levels(actual)
        name = target.name if target.kind == "sink" else self.apps[
            target.key if target.kind == "app" else target.name].identity.name
        output = self._output(actual)[1] if target.kind != "sink" else name
        self.notify(f"{name} · {level_text(level)} → {output}",
                    "单条音频流" if target.kind == "stream" else "",
                    None if level["mixed_volume"] else level["volume"], "control")
        return "done"

    def route(self, key: str, sink_name: str) -> None:
        self.refresh()
        app = self.apps.get(key)
        sink = self.snapshot.sink_by_name(sink_name)
        if not app:
            raise CommandError("应用音频流已退出")
        if not sink or not sink.available:
            raise CommandError("所选输出设备不可用")
        # Save user intent first, so even a partial move never resurrects an
        # older preference. The UI still shows actual, not intended, routing.
        try:
            self.preferences.set(key, sink.name, sink.label, app.identity.name)
        except StorageError as error:
            raise CommandError(str(error)) from error
        self._routes.pop(key, None)
        self.refresh()
        if not self.connected or key in self.routing_errors:
            raise CommandError(self.error or self.routing_errors[key])
        actual = self.apps.get(key)
        current_sink = self.snapshot.sink_by_name(sink_name)
        if not actual or not current_sink or any(s.sink != current_sink.index for s in actual.streams):
            raise CommandError("输出偏好已保存，但应用音频流尚未完成迁移")
        self.notify(f"{actual.identity.name} → {current_sink.label}", "已记住该应用的输出设备", None, "control")

    def forget(self, key: str) -> None:
        self.refresh()
        app = self.apps.get(key)
        if not app:
            raise CommandError("应用暂无音频流；可在偏好文件中移除该记录")
        try:
            self.preferences.forget(key)
        except StorageError as error:
            raise CommandError(str(error)) from error
        self._routes.pop(key, None)
        errors = []
        default = self.snapshot.sink_by_name(self.snapshot.default_sink)
        if default and default.available:
            for stream in app.streams:
                try:
                    self.backend.move(stream.index, default.index)
                except AudioError as error:
                    errors.append(str(error))
        try:
            self.backend.forget_restore_devices(app.streams)
        except AudioError as error:
            errors.append(f"无法清除音频服务的旧路由记忆：{error}")
        self.refresh()
        actual = self.apps.get(key)
        if default and actual and any(s.sink != default.index for s in actual.streams):
            errors.append("当前音频流未能回到默认输出")
        if errors or not self.connected:
            raise CommandError("已取消本服务的输出记忆；" + ("；".join(errors) or self.error))
        self.notify(f"{app.identity.name} · 已取消输出记忆",
                    "恢复跟随系统默认路由策略" if default and default.available else "等待默认输出设备；偏好已取消",
                    None, "control")

    def set_default(self, name: str) -> None:
        self.refresh()
        sink = self.snapshot.sink_by_name(name)
        if not sink or not sink.available:
            raise CommandError("输出设备不可用")
        try:
            self.backend.set_default(name)
        except AudioError as error:
            self.refresh(reconcile=False)
            raise CommandError(str(error)) from error
        self.refresh()
        if not self.connected or self.snapshot.default_sink != name:
            raise CommandError(self.error or "默认输出设备未按请求更新")
        self.notify(f"默认输出 → {sink.label}", "不会统一迁移已有应用的声音", None, "control")

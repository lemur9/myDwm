"""Native GTK3 view. All policy and audio writes remain in the session service.

Rows are keyed and updated in place. Slider writes are serialized/throttled;
server updates never replace a dragged or still-pending slider value.
"""
from __future__ import annotations

import json
import logging
import os
import weakref
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

import gi

gi.require_version("Gtk", "3.0")
gi.require_version("Gdk", "3.0")
from gi.repository import Gdk, GLib, Gtk, Pango  # noqa: E402

from .ipc import IPCError, MAX_RESPONSE, connect, encode, request  # noqa: E402
from .model import level_text, panel_geometry  # noqa: E402
from .storage import Paths  # noqa: E402


def label(text: str = "", style: str = "", *, wrap: bool = False) -> Gtk.Label:
    widget = Gtk.Label(label=text, xalign=0)
    if style:
        widget.get_style_context().add_class(style)
    widget.set_hexpand(True)
    if wrap:
        widget.set_line_wrap(True)
        widget.set_line_wrap_mode(Pango.WrapMode.WORD_CHAR)
    else:
        widget.set_ellipsize(Pango.EllipsizeMode.END)
    return widget


def button(text: str, callback, tooltip: str = "") -> Gtk.Button:
    widget = Gtk.Button(label=text)
    widget.connect("clicked", lambda _widget: callback())
    if tooltip:
        widget.set_tooltip_text(tooltip)
    return widget


def box(vertical: bool = False, spacing: int = 8) -> Gtk.Box:
    return Gtk.Box(orientation=Gtk.Orientation.VERTICAL if vertical else Gtk.Orientation.HORIZONTAL,
                   spacing=spacing)


class VolumeControl(Gtk.Box):
    def __init__(self, panel: Panel, target: dict):
        super().__init__(orientation=Gtk.Orientation.HORIZONTAL, spacing=8)
        self.panel, self.target = panel, target
        self.updating = self.dragging = self.dead = False
        self.pending = False
        self.serial = 0
        self.timer = 0
        self.actual: dict | None = None
        self.writable = True
        self.scale = Gtk.Scale.new_with_range(Gtk.Orientation.HORIZONTAL, 0, 100, 1)
        self.scale.set_draw_value(False)
        self.scale.set_hexpand(True)
        self.scale.set_round_digits(0)
        self.scale.set_tooltip_text("0–100%；调节音量不会隐式取消静音")
        self.scale.connect("button-press-event", self._press)
        self.scale.connect("button-release-event", self._release)
        self.scale.connect("value-changed", self._changed)
        self.value = label()
        self.value.set_hexpand(False)
        self.value.set_width_chars(13)
        self.mute = button("静音", self._mute)
        self.pack_start(self.scale, True, True, 0)
        self.pack_start(self.value, False, False, 0)
        self.pack_end(self.mute, False, False, 0)
        self.connect("destroy", self._destroy)

    def _destroy(self, _widget) -> None:
        self.dead = True
        if self.timer:
            GLib.source_remove(self.timer)
            self.timer = 0

    def _press(self, _scale, event) -> bool:
        if event.button == 1:
            self.dragging = True
        return False

    def _release(self, _scale, event) -> bool:
        if event.button == 1:
            # Run after Gtk.Range's own release handler has finalized its value.
            GLib.idle_add(self._finish_drag)
        return False

    def _finish_drag(self) -> bool:
        self.dragging = False
        if self.timer:
            GLib.source_remove(self.timer)
            self.timer = 0
            self._flush()
        return False

    def _changed(self, _scale) -> None:
        if self.updating or self.dead:
            return
        self.pending = True
        self.serial += 1
        if not self.timer:
            self.timer = GLib.timeout_add(90, self._flush)

    def _flush(self) -> bool:
        self.timer = 0
        if self.dead:
            return False
        serial = self.serial
        message = dict(self.target, op="set-volume", value=round(self.scale.get_value()))

        def complete(_response):
            if not self.dead and serial == self.serial:
                self.pending = False
                if self.actual:
                    self.update(self.actual, self.writable)
        self.panel.command(message, complete)
        return False

    def _mute(self) -> None:
        if self.actual:
            self.panel.command(dict(self.target, op="set-mute", value=self.actual["muted"] is not True))

    def update(self, level: dict, writable: bool) -> None:
        self.actual, self.writable = level, writable
        self.updating = True
        if not self.dragging and not self.pending:
            self.scale.set_value(min(100, level["volume"]))
        self.value.set_text(level["text"])
        self.value.set_tooltip_text(level_text(level) + ("；声道音量存在差异" if level["channel_mixed"] else ""))
        self.mute.set_label("取消静音" if level["muted"] is True else
                            "部分静音" if level["muted"] is None else "静音")
        self.mute.set_tooltip_text("点击统一静音" if level["muted"] is None else "静音与音量为独立状态")
        context = self.mute.get_style_context()
        (context.add_class if level["muted"] is True else context.remove_class)("accent")
        self.scale.set_sensitive(writable)
        self.mute.set_sensitive(writable)
        self.updating = False


class StreamRow(Gtk.Box):
    def __init__(self, panel: Panel, app: str, index: int):
        super().__init__(orientation=Gtk.Orientation.VERTICAL, spacing=4)
        self.get_style_context().add_class("stream")
        self.title = label(style="dim")
        self.control = VolumeControl(panel, {"kind": "stream", "key": index, "app": app})
        self.pack_start(self.title, False, False, 0)
        self.pack_start(self.control, False, False, 0)

    def update(self, stream: dict) -> None:
        status = "播放中" if stream["playing"] else "已暂停"
        self.title.set_text(f"#{stream['index']} · {stream['media'] or '未提供媒体名称'} · {status}"
                            f" · {stream['rate']} Hz / {stream['channels']}ch → {stream['output']}")
        self.title.set_tooltip_text(self.title.get_text() +
                                   "\n声道：" + " / ".join(f"{v}%" for v in stream["channel_values"]))
        self.control.update(stream["level"], stream["writable"])


class AppRow(Gtk.ListBoxRow):
    def __init__(self, panel: Panel, key: str):
        super().__init__()
        self.panel, self.key = panel, key
        self.data: dict = {}
        self.rank = 0
        self.updating = False
        self.choices = None
        self.streams: dict[int, StreamRow] = {}
        content = box(True)
        self.add(content)
        heading = box()
        self.icon = Gtk.Image()
        self.name = label(style="app-name")
        self.lock = button("锁定", self._lock, "显式锁定；仅浏览或展开音频流不会改变控制目标")
        heading.pack_start(self.icon, False, False, 0)
        heading.pack_start(self.name, True, True, 0)
        heading.pack_end(self.lock, False, False, 0)
        content.pack_start(heading, False, False, 0)
        self.media = label(style="dim")
        content.pack_start(self.media, False, False, 0)
        self.control = VolumeControl(panel, {"kind": "app", "key": key})
        content.pack_start(self.control, False, False, 0)
        output = box()
        output.pack_start(label("输出", "dim"), False, False, 0)
        self.output = Gtk.ComboBoxText()
        self.output.set_hexpand(True)
        self.output.set_tooltip_text("选择后迁移该应用的全部音频流，并长期记住设备")
        self.output.connect("changed", self._route)
        panel.combos.add(self.output)
        output.pack_start(self.output, True, True, 0)
        self.remember = button("记住当前", self._remember)
        output.pack_end(self.remember, False, False, 0)
        content.pack_start(output, False, False, 0)
        self.preference = label(style="dim", wrap=True)
        content.pack_start(self.preference, False, False, 0)
        self.expander = Gtk.Expander(label="音频流")
        self.stream_box = box(True)
        self.expander.add(self.stream_box)
        content.pack_start(self.expander, False, False, 0)

    def _lock(self) -> None:
        if self.data.get("locked") and not self.panel.choosing:
            self.panel.command({"op": "unlock"})
        else:
            self.panel.command({"op": "lock", "key": self.key})

    def _route(self, _combo) -> None:
        name = self.output.get_active_id()
        if not self.updating and name and name != "__mixed__":
            self.panel.command({"op": "route", "key": self.key, "sink": name})

    def _remember(self) -> None:
        if self.data.get("preference"):
            self.panel.command({"op": "forget", "key": self.key})
        elif len(self.data.get("outputs", [])) == 1:
            self.panel.command({"op": "route", "key": self.key, "sink": self.data["outputs"][0]["name"]})

    def update(self, app: dict, sinks: list[dict]) -> None:
        self.data = app
        self.name.set_text(app["name"] + (" · 当前目标" if app["is_target"] else ""))
        self.name.set_tooltip_text(app["name"] + "\n身份：" + app["key"])
        self.icon.set_from_icon_name(app["icon"], Gtk.IconSize.LARGE_TOOLBAR)
        media = list(dict.fromkeys(s["media"] for s in app["streams"] if s["media"]))
        text = " / ".join(media[:2])
        status = "播放中" if app["playing"] else "已暂停"
        self.media.set_text(f"{status} · {len(app['streams'])} 条流" + (f" · {text}" if text else ""))
        self.media.set_tooltip_text("\n".join(media) or "音频服务没有提供媒体信息；不推测浏览器标签页")
        self.lock.set_label("选择并锁定" if self.panel.choosing else "解锁" if app["locked"] else "锁定")
        self.lock.set_sensitive(app["writable"])
        context = self.lock.get_style_context()
        (context.add_class if app["locked"] else context.remove_class)("accent")
        self.control.update(app["level"], app["writable"])
        if not self.output.get_property("popup-shown"):
            self.updating = True
            choices = [("__mixed__", "多个输出 / 等待设备")] + [
                (s["name"], s["label"]) for s in sinks if s["available"]]
            if choices != self.choices:
                self.output.remove_all()
                for name, description in choices:
                    self.output.append(name, description)
                self.choices = choices
            active = app["outputs"][0]["name"] if len(app["outputs"]) == 1 else "__mixed__"
            if active not in {name for name, _label in choices}:
                active = "__mixed__"
            self.output.set_active_id(active)
            self.updating = False
        pref = app["preference"]
        self.remember.set_label("取消记忆" if pref else "记住当前")
        self.remember.set_sensitive(bool(pref) or (len(app["outputs"]) == 1 and app["outputs"][0]["available"]))
        if app["route_error"]:
            self.preference.set_text("输出切换失败：" + app["route_error"])
        elif pref:
            remembered = next((s for s in sinks if s["name"] == pref["sink"] and s["available"]), None)
            self.preference.set_text("已记住：" + pref.get("sink_label", pref["sink"]) +
                                     ("" if remembered else " · 设备不可用，偏好保留；当前 → " + app["output"]))
        else:
            self.preference.set_text("未记忆专属设备 · 跟随系统路由策略")
        self.expander.set_label(f"音频流 ({len(app['streams'])}) · 可分别调整音量与静音")
        indexes = {s["index"] for s in app["streams"]}
        for index in set(self.streams) - indexes:
            self.streams.pop(index).destroy()
        for stream in app["streams"]:
            index = stream["index"]
            if index not in self.streams:
                row = self.streams[index] = StreamRow(self.panel, self.key, index)
                self.stream_box.pack_start(row, False, False, 0)
                row.show_all()
            self.streams[index].update(stream)


class DeviceRow(Gtk.ListBoxRow):
    def __init__(self, panel: Panel, name: str):
        super().__init__()
        self.rank = 0
        content = box(True)
        self.add(content)
        heading = box()
        self.name = label(style="app-name")
        self.default = button("设为默认输出", lambda: panel.command({"op": "default", "sink": name}))
        heading.pack_start(Gtk.Image.new_from_icon_name("audio-card", Gtk.IconSize.LARGE_TOOLBAR), False, False, 0)
        heading.pack_start(self.name, True, True, 0)
        heading.pack_end(self.default, False, False, 0)
        content.pack_start(heading, False, False, 0)
        self.status = label(style="dim")
        content.pack_start(self.status, False, False, 0)
        self.control = VolumeControl(panel, {"kind": "sink", "key": name})
        content.pack_start(self.control, False, False, 0)

    def update(self, sink: dict) -> None:
        self.name.set_text(sink["label"])
        self.name.set_tooltip_text(sink["label"] + "\n" + sink["name"])
        self.default.set_label("默认输出" if sink["default"] else "设为默认输出")
        self.default.set_sensitive(sink["available"] and not sink["default"])
        states = {"running": "使用中", "idle": "空闲", "suspended": "空闲挂起（仍可用）"}
        self.status.set_text(("可用 · " + states.get(sink["state"], sink["state"])) if sink["available"] else "不可用")
        self.control.update(sink["level"], sink["available"])


class OutsideClicks:
    """Passive XI2 raw button events, including clicks on empty desktop/bar.

    No pointer/keyboard grabs: dwm shortcuts and the underlying click still work.
    Focus-out remains the fallback for X servers without XInput2.
    """
    def __init__(self, panel: Panel):
        from Xlib import display
        from Xlib.ext import xinput
        self.panel, self.xinput = panel, xinput
        self.display = display.Display()
        self.source = 0
        try:
            self.display.xinput_query_version()
            self.opcode = self.display.query_extension("XInputExtension").major_opcode
            self.display.screen().root.xinput_select_events([(xinput.AllMasterDevices, xinput.RawButtonPressMask)])
            self.display.flush()
            self.source = GLib.io_add_watch(self.display.fileno(), GLib.IO_IN | GLib.IO_HUP | GLib.IO_ERR, self._read)
        except Exception:
            self.display.close()
            raise

    def _read(self, _fd, condition) -> bool:
        try:
            if condition & (GLib.IO_HUP | GLib.IO_ERR):
                self.source = 0
                return False
            while self.display.pending_events():
                event = self.display.next_event()
                if (getattr(event, "extension", None) == self.opcode and
                        getattr(event, "evtype", None) == self.xinput.RawButtonPress and
                        not self.panel.popup_shown()):
                    point = self.display.screen().root.query_pointer()
                    x, y = self.panel.window.get_position()
                    width, height = self.panel.window.get_size()
                    if not (x - 3 <= point.root_x <= x + width + 3 and y - 3 <= point.root_y <= y + height + 3):
                        generation = self.panel.generation
                        # Let a bar entry switch pages/toggle the same instance
                        # before classifying the click as a generic dismissal.
                        GLib.timeout_add(200, self.panel.dismiss_if_unchanged, generation)
            return True
        except Exception:
            logging.debug("Outside-click tracking unavailable", exc_info=True)
            self.source = 0
            return False

    def close(self) -> None:
        if self.source:
            GLib.source_remove(self.source)
            self.source = 0
        self.display.close()


class Panel:
    def __init__(self, paths: Paths, initial: dict):
        self.paths = paths
        self.closing = False
        self.state: dict = {}
        self.revision = -1
        self.generation = 0
        self.choosing = False
        self.updating = False
        self.combos = weakref.WeakSet()
        self.app_rows: dict[str, AppRow] = {}
        self.sink_rows: dict[str, DeviceRow] = {}
        self.pool = ThreadPoolExecutor(max_workers=1, thread_name_prefix="audio-panel-commands")
        self.socket = connect(paths.socket)
        self.socket.sendall(encode({"op": "subscribe"}))
        self.socket.setblocking(False)
        self.buffer = bytearray()
        self.source = GLib.io_add_watch(self.socket.fileno(), GLib.IO_IN | GLib.IO_HUP | GLib.IO_ERR, self._receive)
        self.window = Gtk.Window(type=Gtk.WindowType.TOPLEVEL)
        self.window.set_wmclass("dwm-audio", "DwmAudio")
        self.window.set_title("声音控制")
        self.window.set_decorated(False)
        self.window.set_resizable(False)
        self.window.set_skip_taskbar_hint(True)
        self.window.set_skip_pager_hint(True)
        self.window.set_keep_above(True)
        self.window.set_type_hint(Gdk.WindowTypeHint.DIALOG)
        self.window.get_style_context().add_class("audio-panel")
        self.window.connect("delete-event", lambda *_args: self.close() or True)
        self.window.connect("key-press-event", self._key)
        self.window.connect("focus-out-event", self._focus_out)
        provider = Gtk.CssProvider()
        provider.load_from_path(str(Path(__file__).with_name("panel.css")))
        Gtk.StyleContext.add_provider_for_screen(Gdk.Screen.get_default(), provider, Gtk.STYLE_PROVIDER_PRIORITY_APPLICATION)
        outer = box(True, 12)
        for side in ("top", "bottom", "start", "end"):
            getattr(outer, "set_margin_" + side)(16)
        self.window.add(outer)
        heading = box()
        heading.pack_start(label("声音", "heading"), True, True, 0)
        self.apps_tab = button("应用", lambda: self.command({"op": "panel-page", "page": "apps"}), "Ctrl+1")
        self.devices_tab = button("设备", lambda: self.command({"op": "panel-page", "page": "devices"}), "Ctrl+2")
        for widget in (self.apps_tab, self.devices_tab, button("关闭", self.close, "Esc")):
            heading.pack_start(widget, False, False, 0)
        outer.pack_start(heading, False, False, 0)
        target = box()
        self.target = label(style="target", wrap=True)
        target.pack_start(self.target, True, True, 0)
        self.unlock = button("解除锁定", lambda: self.command({"op": "unlock"}), "Ctrl+U")
        target.pack_end(self.unlock, False, False, 0)
        outer.pack_start(target, False, False, 0)
        self.banner = label("选择要控制的应用 · 选择后锁定，本次按键未改动音量\nTab / 方向键导航，Enter 选择，Esc 取消", "chooser", wrap=True)
        self.banner.set_no_show_all(True)
        outer.pack_start(self.banner, False, False, 0)
        self.error = label(style="error", wrap=True)
        self.error.set_no_show_all(True)
        outer.pack_start(self.error, False, False, 0)
        self.stack = Gtk.Stack()
        self.lists = {}
        for name, placeholder in (("apps", "暂无应用音频流；智能操作将控制默认输出设备"),
                                  ("devices", "等待输出设备连接")):
            scroll = Gtk.ScrolledWindow()
            scroll.set_policy(Gtk.PolicyType.AUTOMATIC, Gtk.PolicyType.AUTOMATIC)
            listing = Gtk.ListBox()
            listing.set_selection_mode(Gtk.SelectionMode.SINGLE)
            listing.set_activate_on_single_click(False)
            listing.set_sort_func(lambda a, b: (a.rank > b.rank) - (a.rank < b.rank))
            listing.set_placeholder(label(placeholder, "dim", wrap=True))
            listing.connect("row-activated", self._row_activated)
            scroll.add(listing)
            self.stack.add_named(scroll, name)
            self.lists[name] = listing
        outer.pack_start(self.stack, True, True, 0)
        outer.pack_end(label("0–100% · 音量与静音独立 · 默认输出变更不会统一迁移应用", "dim", wrap=True), False, False, 0)
        self.apply(initial)
        self._position()
        if not self.closing:
            self.window.show_all()
        self._visibility()
        self.outside = None
        try:
            self.outside = OutsideClicks(self)
        except Exception:
            logging.debug("XInput2 unavailable; using focus-out dismissal", exc_info=True)

    def _visibility(self) -> None:
        self.banner.set_visible(self.choosing)
        self.unlock.set_visible(bool(self.state.get("lock")))
        self.error.set_visible(bool(self.error.get_text()))

    def _position(self) -> None:
        panel = self.state["panel"]
        x, y = panel["anchor"]
        display = self.window.get_display()
        monitor = display.get_monitor_at_point(x, y) or display.get_primary_monitor() or display.get_monitor(0)
        area = monitor.get_workarea()
        workarea = (area.x, area.y, area.width, area.height)
        _left, _top, width, height = panel_geometry(workarea, (x, y), panel["origin"])
        self.window.set_default_size(width, height)

        def place():
            if self.closing:
                return False
            actual = self.window.get_size()
            left, top, width, height = panel_geometry(workarea, (x, y), panel["origin"], actual)
            if actual != (width, height):
                self.window.resize(width, height)
            self.window.move(left, top)
            self.window.present()
            return False
        GLib.idle_add(place)

    def _key(self, _window, event) -> bool:
        if event.keyval == Gdk.KEY_Escape and not self.popup_shown():
            self.close()
            return True
        if event.state & Gdk.ModifierType.CONTROL_MASK:
            if event.keyval in (Gdk.KEY_1, Gdk.KEY_2):
                self.command({"op": "panel-page", "page": "apps" if event.keyval == Gdk.KEY_1 else "devices"})
                return True
            if event.keyval in (Gdk.KEY_u, Gdk.KEY_U):
                self.command({"op": "unlock"})
                return True
        return False

    def _row_activated(self, listing, row) -> None:
        if self.choosing and listing is self.lists["apps"] and isinstance(row, AppRow):
            self.command({"op": "lock", "key": row.key})

    def popup_shown(self) -> bool:
        return any(combo.get_property("popup-shown") for combo in self.combos)

    def _focus_out(self, *_args) -> bool:
        def check():
            if not self.closing and not self.window.is_active() and not self.popup_shown():
                self.close()
            return False
        GLib.timeout_add(180, check)
        return False

    def dismiss_if_unchanged(self, generation: int) -> bool:
        if not self.closing and generation == self.generation and not self.popup_shown():
            self.close()
        return False

    def command(self, message: dict, complete=None) -> None:
        if self.closing:
            return

        def work():
            try:
                response = request(self.paths.socket, message, timeout=4.0)
            except IPCError as error:
                response = {"ok": False, "error": str(error)}

            def finish():
                if self.closing:
                    return False
                if response.get("state"):
                    self.apply(response["state"])
                self.error.set_text("" if response.get("ok") else response.get("error", "操作未完成"))
                if complete:
                    complete(response)
                self._visibility()
                return False
            GLib.idle_add(finish)
        self.pool.submit(work)

    def _receive(self, _fd, condition) -> bool:
        try:
            if condition & (GLib.IO_HUP | GLib.IO_ERR):
                raise IPCError("声音服务连接中断")
            while True:
                try:
                    part = self.socket.recv(65536)
                except BlockingIOError:
                    break
                if not part:
                    raise IPCError("声音服务已结束")
                self.buffer.extend(part)
                if len(self.buffer) > MAX_RESPONSE:
                    raise IPCError("声音状态过大")
                while b"\n" in self.buffer:
                    line, _, remaining = self.buffer.partition(b"\n")
                    self.buffer = bytearray(remaining)
                    event = json.loads(line)
                    if event.get("event") == "state":
                        self.apply(event["state"])
            return not self.closing
        except (OSError, ValueError, IPCError) as error:
            self.source = 0
            if not self.closing:
                self.error.set_text(str(error))
                self.error.show()
                self.stack.set_sensitive(False)
                GLib.timeout_add(1600, self.close)
            return False

    def apply(self, state: dict) -> None:
        if state.get("revision", 0) <= self.revision or self.closing:
            return
        self.state, self.revision = state, state.get("revision", 0)
        panel = state["panel"]
        self.generation = panel["generation"]
        self.choosing = panel["choose"]
        if not panel["open"]:
            self.close()
            return
        self.stack.set_visible_child_name(panel["page"])
        for widget, page in ((self.apps_tab, "apps"), (self.devices_tab, "devices")):
            context = widget.get_style_context()
            (context.add_class if page == panel["page"] else context.remove_class)("accent")
        target = state["target"]
        description = level_text(target["level"]) + " → " + target["output"] if target["level"] else target["reason"]
        self.target.set_text(("已锁定 · " if target["locked"] else "当前目标 · ") + target["name"] + "\n" + description)
        self.error.set_text(state["error"])
        self.stack.set_sensitive(state["connected"])
        current = {a["key"] for a in state["apps"]}
        for key in set(self.app_rows) - current:
            self.app_rows.pop(key).destroy()
        for rank, app in enumerate(state["apps"]):
            if app["key"] not in self.app_rows:
                row = self.app_rows[app["key"]] = AppRow(self, app["key"])
                self.lists["apps"].add(row)
                row.show_all()
            row = self.app_rows[app["key"]]
            row.rank = rank
            row.update(app, state["sinks"])
        current = {s["name"] for s in state["sinks"]}
        for name in set(self.sink_rows) - current:
            self.sink_rows.pop(name).destroy()
        for rank, sink in enumerate(state["sinks"]):
            if sink["name"] not in self.sink_rows:
                row = self.sink_rows[sink["name"]] = DeviceRow(self, sink["name"])
                self.lists["devices"].add(row)
                row.show_all()
            row = self.sink_rows[sink["name"]]
            row.rank = rank
            row.update(sink)
        for listing in self.lists.values():
            listing.invalidate_sort()
        self._visibility()

    def close(self) -> bool:
        if not self.closing:
            self.closing = True
            self.window.hide()
            if Gtk.main_level():
                Gtk.main_quit()
        return False

    def dispose(self) -> None:
        if self.source:
            GLib.source_remove(self.source)
        if self.outside:
            self.outside.close()
        self.socket.close()
        self.window.destroy()
        self.pool.shutdown(wait=False, cancel_futures=True)
        try:
            request(self.paths.socket, {"op": "panel-closed", "pid": os.getpid()}, timeout=0.5)
        except IPCError:
            pass


def run_panel(paths: Paths) -> int:
    ready, _args = Gtk.init_check([])
    if not ready:
        raise RuntimeError("无法连接 GTK3 / X11 桌面")
    response = request(paths.socket, {"op": "state"})
    initial = response["state"]
    if not initial["panel"]["open"]:
        return 0
    panel = Panel(paths, initial)
    try:
        if not panel.closing:
            panel.command({"op": "panel-ready", "pid": os.getpid()})
            Gtk.main()
    finally:
        panel.dispose()
    return 0

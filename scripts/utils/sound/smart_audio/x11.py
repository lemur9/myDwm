"""Event-driven active-window tracking; the sound panel is never a target."""
from __future__ import annotations

from .model import Window


class X11Error(Exception):
    pass


class X11Monitor:
    def __init__(self):
        try:
            from Xlib import X, Xatom, display, error
            self.X, self.Xatom, self.errors = X, Xatom, error
            self.display = display.Display()
            self.root = self.display.screen().root
            self.active_atom = self.display.intern_atom("_NET_ACTIVE_WINDOW")
            self.pid_atom = self.display.intern_atom("_NET_WM_PID")
            self.root.change_attributes(event_mask=X.PropertyChangeMask)
            self.display.flush()
        except ImportError as exc:
            raise X11Error("需要 python-xlib；请参见 docs/smart-audio.md") from exc
        except Exception as exc:
            raise X11Error(f"无法连接 X11 桌面，请检查 DISPLAY / XAUTHORITY：{exc}") from exc

    def fileno(self) -> int:
        return self.display.fileno()

    def current(self) -> Window:
        try:
            prop = self.root.get_full_property(self.active_atom, self.Xatom.WINDOW)
            if prop is None or not len(prop.value) or not int(prop.value[0]):
                return Window()
            xid = int(prop.value[0])
            win = self.display.create_resource_object("window", xid)
            pid = win.get_full_property(self.pid_atom, self.Xatom.CARDINAL)
            classes = tuple(win.get_wm_class() or ())
            geometry = win.get_geometry()
            # translate_coords is invoked on the DESTINATION window in Xlib.
            position = self.root.translate_coords(win, 0, 0)
            return Window(xid, int(pid.value[0]) if pid is not None and len(pid.value) else 0,
                          classes, (position.x + geometry.width // 2,
                                    position.y + geometry.height // 2))
        except (self.errors.BadWindow, self.errors.BadDrawable, self.errors.BadMatch):
            return Window()  # Window disappeared between the property reads.
        except (self.errors.ConnectionClosedError, OSError) as exc:
            raise X11Error("X11 会话已结束") from exc

    def pointer(self) -> tuple[int, int]:
        position = self.root.query_pointer()
        return int(position.root_x), int(position.root_y)

    def drain(self) -> bool:
        changed = False
        try:
            while self.display.pending_events():
                event = self.display.next_event()
                if event.type == self.X.PropertyNotify and event.atom == self.active_atom:
                    changed = True
        except (self.errors.ConnectionClosedError, OSError) as exc:
            raise X11Error("X11 会话已结束") from exc
        return changed

    def close(self) -> None:
        self.display.close()

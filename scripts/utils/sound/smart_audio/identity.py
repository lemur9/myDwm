"""Stable application identities and conservative X11/process association.

Never infer browser tabs from titles. A shared shell/session ancestor is NOT
sufficient evidence that two processes belong to the same audio application.
"""
from __future__ import annotations

import os
import re
import unicodedata
from dataclasses import dataclass
from pathlib import Path


# Exact aliases only; in particular Chromium and Chrome remain separate apps.
ALIASES = {
    "chrome": "google-chrome", "google-chrome-stable": "google-chrome",
    "googlechrome": "google-chrome", "google chrome": "google-chrome",
    "com.google.chrome": "google-chrome",
    "chromium-browser": "chromium", "org.chromium.chromium": "chromium",
    "firefox-esr": "firefox", "org.mozilla.firefox": "firefox",
    "org.mozilla.firefox-esr": "firefox",
    "io.mpv.mpv": "mpv", "org.videolan.vlc": "vlc",
    "com.spotify.client": "spotify",
}
KNOWN_FAMILIES = frozenset(ALIASES.values())
PROCESS_CONTAINERS = frozenset({"bash", "zsh", "fish", "sh", "dash", "systemd",
                                "st", "xterm", "alacritty", "kitty", "wezterm",
                                "gnome-terminal-server", "konsole"})


def clean_text(value: object, limit: int = 240) -> str:
    """Audio metadata is untrusted, including dwm's status control bytes."""
    text = "".join(c for c in str(value or "")
                   if not unicodedata.category(c).startswith("C"))
    return re.sub(r"\s+", " ", text).strip()[:limit]


def family(value: str) -> str:
    value = os.path.basename(value).casefold().strip()
    if value.endswith(".desktop"):
        value = value[:-8]
    return ALIASES.get(value, value)


@dataclass(frozen=True)
class Process:
    pid: int
    parent: int
    binary: str
    started: str = ""


class Processes:
    """A short-lived /proc cache. Reset it for each audio/focus refresh."""

    def __init__(self, root: Path = Path("/proc")):
        self.root = root
        self.cache: dict[int, Process | None] = {}

    def clear(self) -> None:
        self.cache.clear()

    def get(self, pid: int) -> Process | None:
        if pid not in self.cache:
            try:
                directory = self.root / str(pid)
                # comm may contain spaces or parentheses: split after the LAST ).
                fields = (directory / "stat").read_text().rpartition(")")[2].split()
                try:
                    binary = os.readlink(directory / "exe").removesuffix(" (deleted)")
                except OSError:
                    binary = (directory / "comm").read_text().strip()
                self.cache[pid] = Process(pid, int(fields[1]), family(binary), fields[19])
            except (OSError, ValueError, IndexError):
                self.cache[pid] = None
        return self.cache[pid]

    def ancestors(self, pid: int) -> tuple[Process, ...]:
        result: list[Process] = []
        seen: set[int] = set()
        while pid > 1 and pid not in seen and len(result) < 64:
            seen.add(pid)
            process = self.get(pid)
            if process is None:
                break
            result.append(process)
            pid = process.parent
        return tuple(result)

    def related(self, window_pid: int, stream_pid: int, app_family: str) -> int:
        if not window_pid or not stream_pid:
            return 0
        if window_pid == stream_pid:
            return 100
        window = self.ancestors(window_pid)
        stream = self.ancestors(stream_pid)
        if window_pid in {p.pid for p in stream}:
            owner = self.get(window_pid)
            # A terminal that launched two background players is not itself
            # their audio window. Browser/helper descendants remain associated.
            if not owner or owner.binary not in PROCESS_CONTAINERS or owner.binary == app_family:
                return 90
        if stream_pid in {p.pid for p in window}:
            return 85
        # Browser window and audio processes can be siblings of the browser
        # process, but siblings of bash/systemd are not an application match.
        app_ancestors = {p.pid for p in stream if p.binary == app_family}
        if app_family and any(p.pid in app_ancestors and p.binary == app_family
                              for p in window):
            return 80
        return 0


@dataclass(frozen=True)
class Identity:
    key: str
    name: str
    family: str
    icon: str


def identify(properties: dict[str, str], pid: int, processes: Processes) -> Identity:
    app_id = properties.get("application.id", "").strip()
    binary = properties.get("application.process.binary", "").strip()
    process = processes.get(pid) if pid else None
    binary = binary or (process.binary if process else "")
    name = clean_text(properties.get("application.name") or binary or app_id or "未知应用")
    canonical = family(app_id or binary or name)
    # Plain application IDs and exact known desktop aliases share a namespace
    # with binaries. Opaque desktop IDs stay distinct (e.g. Electron apps).
    if app_id:
        key = ("app:" if canonical in KNOWN_FAMILIES or "." not in app_id else "id:") + canonical
    elif binary or canonical in KNOWN_FAMILIES:
        key = "app:" + canonical
    else:
        key = "name:" + name.casefold()
    return Identity(key, name, family(binary or app_id or name),
                    clean_text(properties.get("application.icon_name") or "audio-x-generic"))

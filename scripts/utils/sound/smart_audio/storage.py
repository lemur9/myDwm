"""Private session IPC paths and atomic, versioned routing preferences."""
from __future__ import annotations

import hashlib
import json
import os
import re
import stat
import tempfile
from dataclasses import dataclass
from pathlib import Path


class StorageError(Exception):
    pass


def private_directory(path: Path) -> Path:
    path.mkdir(mode=0o700, parents=True, exist_ok=True)
    info = path.lstat()
    if not stat.S_ISDIR(info.st_mode) or info.st_uid != os.getuid():
        raise StorageError(f"目录必须属于当前用户，且不能是符号链接：{path}")
    if info.st_mode & 0o077:
        raise StorageError(f"目录权限过宽，请执行 chmod 700 {path}")
    return path


def atomic_json(path: Path, data: dict) -> None:
    private_directory(path.parent)
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(mode="w", encoding="utf-8", dir=path.parent,
                                         prefix=".preferences-", delete=False) as output:
            temporary = Path(output.name)
            os.chmod(temporary, 0o600)
            json.dump(data, output, ensure_ascii=False, indent=2, allow_nan=False)
            output.write("\n")
            output.flush()
            os.fsync(output.fileno())
        os.replace(temporary, path)
    finally:
        if temporary and temporary.exists():
            temporary.unlink()


@dataclass(frozen=True)
class Paths:
    runtime: Path
    preferences: Path

    @classmethod
    def discover(cls) -> Paths:
        display = re.sub(r"\.\d+$", "", os.environ.get("DISPLAY", "no-display"))
        scope = display + "\0" + os.environ.get("XDG_SESSION_ID", "")
        token = hashlib.sha256(scope.encode()).hexdigest()[:12]
        runtime_base = Path(os.environ.get("XDG_RUNTIME_DIR") or tempfile.gettempdir())
        runtime = Path(os.environ.get("DWM_AUDIO_RUNTIME_DIR") or
                       runtime_base / f"dwm-audio-{os.getuid()}-{token}")
        state_base = Path(os.environ.get("XDG_STATE_HOME") or Path.home() / ".local/state")
        state = Path(os.environ.get("DWM_AUDIO_STATE_DIR") or state_base / "dwm-audio")
        return cls(runtime, state / "preferences.json")

    @property
    def socket(self) -> Path:
        return self.runtime / "control.sock"

    @property
    def lock(self) -> Path:
        return self.runtime / "daemon.lock"

    @property
    def log(self) -> Path:
        return self.runtime / "daemon.log"

    def prepare(self) -> None:
        private_directory(self.runtime)
        # sockaddr_un.sun_path is 108 bytes on Linux (including the final NUL).
        if len(os.fsencode(self.socket)) > 103:
            raise StorageError("声音服务运行目录过长，请设置较短的 DWM_AUDIO_RUNTIME_DIR")


class Preferences:
    def __init__(self, path: Path):
        self.path = path
        self.apps: dict[str, dict[str, str]] = {}
        self.error = ""
        try:
            if path.exists():
                if path.stat().st_size > 1024 * 1024:
                    raise ValueError("文件过大")
                data = json.loads(path.read_text(encoding="utf-8"))
                if not isinstance(data, dict) or data.get("version") != 1:
                    raise ValueError("不支持的配置版本")
                apps = data.get("applications")
                if not isinstance(apps, dict):
                    raise ValueError("applications 必须是对象")
                for key, entry in apps.items():
                    if (not isinstance(key, str) or not isinstance(entry, dict)
                            or not isinstance(entry.get("sink"), str) or not entry["sink"]
                            or any(not isinstance(v, str) for v in entry.values())):
                        raise ValueError("无效的应用输出记录")
                self.apps = apps
        except (OSError, ValueError, UnicodeError) as error:
            # Never silently overwrite a corrupt/unsupported user's file.
            self.error = f"无法读取输出偏好（原文件已保留）：{error}；请备份并修复 {path} 后重启声音服务"

    def _save(self, apps: dict[str, dict[str, str]]) -> None:
        if self.error:
            raise StorageError(self.error)
        try:
            atomic_json(self.path, {"version": 1, "applications": apps})
        except OSError as error:
            raise StorageError(f"无法保存输出偏好：{error}") from error
        self.apps = apps

    def set(self, key: str, sink: str, sink_label: str, app_label: str) -> None:
        apps = dict(self.apps)
        apps[key] = {"sink": sink, "sink_label": sink_label, "app_label": app_label}
        self._save(apps)

    def forget(self, key: str) -> None:
        apps = dict(self.apps)
        apps.pop(key, None)
        self._save(apps)

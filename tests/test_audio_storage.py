from __future__ import annotations

import json
import os
import stat
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

import audio_helpers  # noqa: F401 - configure the source tree import path
from smart_audio.identity import Processes
from smart_audio.storage import Paths, Preferences, StorageError, private_directory


class StorageTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.path = self.root / "state/preferences.json"

    def test_preferences_are_atomic_private_json_using_stable_keys(self):
        preferences = Preferences(self.path)
        preferences.set("app:mpv", "alsa.usb", "USB 耳机", "mpv")
        data = json.loads(self.path.read_text())
        self.assertEqual(data["version"], 1)
        self.assertEqual(data["applications"]["app:mpv"]["sink"], "alsa.usb")
        self.assertEqual(stat.S_IMODE(self.path.stat().st_mode), 0o600)
        self.assertEqual(stat.S_IMODE(self.path.parent.stat().st_mode), 0o700)
        self.assertEqual(Preferences(self.path).apps, preferences.apps)
        self.assertFalse(list(self.path.parent.glob(".preferences-*")))

    def test_failed_replace_keeps_both_memory_and_original_file(self):
        preferences = Preferences(self.path)
        preferences.set("app:mpv", "old", "旧", "mpv")
        original = self.path.read_bytes()
        with patch("smart_audio.storage.os.replace", side_effect=OSError("disk full")):
            with self.assertRaisesRegex(StorageError, "disk full"):
                preferences.set("app:mpv", "new", "新", "mpv")
        self.assertEqual(self.path.read_bytes(), original)
        self.assertEqual(preferences.apps["app:mpv"]["sink"], "old")
        self.assertFalse(list(self.path.parent.glob(".preferences-*")))

    def test_corrupt_preferences_are_not_overwritten(self):
        self.path.parent.mkdir(mode=0o700)
        self.path.write_text("not json")
        preferences = Preferences(self.path)
        self.assertTrue(preferences.error)
        with self.assertRaises(StorageError):
            preferences.set("app:mpv", "sink", "sink", "mpv")
        self.assertEqual(self.path.read_text(), "not json")

    def test_unknown_version_or_invalid_schema_is_rejected(self):
        self.path.parent.mkdir(mode=0o700)
        for data in ({"version": 2, "applications": {}}, {"version": 1, "applications": []},
                     {"version": 1, "applications": {"a": {"sink": 3}}}):
            with self.subTest(data=data):
                self.path.write_text(json.dumps(data))
                self.assertTrue(Preferences(self.path).error)

    def test_runtime_is_scoped_by_display_and_desktop_session(self):
        with patch.dict(os.environ, {"XDG_RUNTIME_DIR": str(self.root), "DISPLAY": ":0", "XDG_SESSION_ID": "a"}):
            first = Paths.discover()
            with patch.dict(os.environ, {"DISPLAY": ":0.0"}):
                self.assertEqual(Paths.discover().runtime, first.runtime)
            with patch.dict(os.environ, {"DISPLAY": ":1"}):
                self.assertNotEqual(Paths.discover().runtime, first.runtime)
            with patch.dict(os.environ, {"XDG_SESSION_ID": "b"}):
                self.assertNotEqual(Paths.discover().runtime, first.runtime)

    def test_insecure_runtime_directory_is_rejected(self):
        path = self.root / "runtime"
        path.mkdir(mode=0o755)
        path.chmod(0o755)
        with self.assertRaisesRegex(StorageError, "chmod 700"):
            private_directory(path)

    def test_runtime_symlinks_are_rejected(self):
        link = self.root / "link"
        link.symlink_to(self.root, target_is_directory=True)
        with self.assertRaises(StorageError):
            private_directory(link)

    def test_proc_parser_handles_parentheses_spaces_and_deleted_exe(self):
        directory = self.root / "77"
        directory.mkdir()
        # Fields 3..22: state, ppid, ..., starttime. comm includes a closing ).
        fields = ["S", "42"] + ["0"] * 17 + ["123456"]
        (directory / "stat").write_text("77 (audio helper) worker) " + " ".join(fields))
        (directory / "exe").symlink_to("/usr/bin/google-chrome (deleted)")
        process = Processes(self.root).get(77)
        self.assertEqual((process.parent, process.binary, process.started), (42, "google-chrome", "123456"))

    def test_process_cache_can_be_reset_for_pid_reuse(self):
        processes = Processes(self.root)
        self.assertIsNone(processes.get(77))
        directory = self.root / "77"
        directory.mkdir()
        fields = ["S", "42"] + ["0"] * 17 + ["123456"]
        (directory / "stat").write_text("77 (mpv) " + " ".join(fields))
        (directory / "comm").write_text("mpv")
        self.assertIsNone(processes.get(77))
        processes.clear()
        self.assertEqual(processes.get(77).binary, "mpv")


if __name__ == "__main__":
    unittest.main()

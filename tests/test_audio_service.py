from __future__ import annotations

import fcntl
import json
import os
import selectors
import struct
import subprocess
import tempfile
import threading
import unittest
from dataclasses import replace
from pathlib import Path
from unittest.mock import Mock, patch

from audio_helpers import (ROOT, SPEAKERS, USB, FakeBackend, FakeMonitor,
                           FakeProcesses, Snapshot, Window, stream)
from smart_audio.engine import Engine
from smart_audio.ipc import IPCError, JsonServer, connect, encode, request
from smart_audio.service import Service, run_daemon
from smart_audio.storage import Paths, Preferences


class ServiceCase(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.paths = Paths(Path(self.temporary.name) / "run", Path(self.temporary.name) / "state/preferences.json")
        self.paths.prepare()
        self.backend = FakeBackend(Snapshot((SPEAKERS, USB), (stream(),), SPEAKERS.name))
        self.monitor = FakeMonitor(Window(1, 100, ("mpv",), (600, 400)))
        self.notices = []
        self.engine = Engine(self.backend, Preferences(self.paths.preferences), FakeProcesses(),
                             lambda *args: self.notices.append(args))
        self.service = Service(self.paths, self.engine, self.monitor, audio_events=False)
        self.engine.set_window(self.monitor.current())
        self.engine.refresh()
        self.service.publish()


class DirectServiceTests(ServiceCase):
    def tearDown(self):
        self.service.close()

    def test_ambiguity_opens_chooser_without_replaying_original_delta(self):
        self.backend.data = replace(self.backend.data, streams=(stream(), stream(20, "spotify", 200)))
        self.monitor.window = Window(9, 900, ("Editor",), (600, 400))
        with patch("smart_audio.service.subprocess.Popen") as popen:
            popen.return_value.pid = 777
            popen.return_value.poll.return_value = None
            response = self.service.handle({"op": "adjust", "delta": 5})
            self.assertTrue(response["ok"])
            self.assertEqual(response["result"], "choose")
            self.assertTrue(self.service.panel["choose"])
            self.assertEqual(self.backend.calls, [])
            self.service.handle({"op": "lock", "key": "app:mpv"})
            self.assertEqual(self.backend.calls, [])
            self.assertFalse(self.service.panel["open"])
            self.assertEqual(self.engine.lock.key, "app:mpv")
            self.service.handle({"op": "adjust", "delta": 5})
            self.assertEqual(self.backend.calls[-1], ("volume", "stream", 10, 55))

    def test_cancel_and_normal_browsing_do_not_create_lock(self):
        with patch("smart_audio.service.subprocess.Popen") as popen:
            popen.return_value.poll.return_value = None
            self.service.handle({"op": "panel", "page": "apps"})
            self.assertIsNone(self.engine.lock)
            self.service.handle({"op": "panel", "page": "apps"})
            self.assertFalse(self.service.panel["open"])
            self.assertIsNone(self.engine.lock)
            self.assertEqual(self.backend.calls, [])

    def test_panel_context_is_frozen_before_focus_changes(self):
        self.backend.data = replace(self.backend.data, streams=(stream(), stream(20, "spotify", 200)))
        with patch("smart_audio.service.subprocess.Popen") as popen:
            popen.return_value.poll.return_value = None
            self.service.handle({"op": "panel", "page": "apps", "origin": "window"})
            self.monitor.window = Window(22, 999, ("DwmAudio",), (1400, 30))
            self.service.handle({"op": "mute"})
            self.assertEqual(self.backend.calls[-1], ("mute", "stream", 10, True))
            self.assertEqual(self.service.panel["anchor"], [600, 400])

    def test_empty_tag_uses_pointer_monitor_for_keyboard_chooser(self):
        self.monitor.window = Window()
        with patch("smart_audio.service.subprocess.Popen") as popen:
            popen.return_value.poll.return_value = None
            self.service.open_panel("apps", choose=True, origin="window")
            self.assertEqual(self.service.panel["anchor"], [1400, 30])
            self.assertEqual(self.service.panel["origin"], "pointer")

    def test_same_entry_toggles_and_other_entry_changes_page_in_one_process(self):
        with patch("smart_audio.service.subprocess.Popen") as popen:
            popen.return_value.poll.return_value = None
            self.service.handle({"op": "panel", "page": "apps"})
            generation = self.service.panel["generation"]
            self.service.handle({"op": "panel", "page": "devices"})
            self.assertEqual(popen.call_count, 1)
            self.assertEqual(self.service.panel["page"], "devices")
            self.assertGreater(self.service.panel["generation"], generation)
            self.service.handle({"op": "panel", "page": "devices"})
            self.assertFalse(self.service.panel["open"])

    def test_invalid_ipc_parameters_do_not_touch_audio(self):
        for message in ({"op": "adjust", "delta": True}, {"op": "adjust", "delta": 999},
                        {"op": "mute", "scope": "all"},
                        {"op": "set-volume", "kind": "sink", "key": SPEAKERS.name, "value": float("nan")},
                        {"op": "set-mute", "kind": "app", "key": "app:mpv", "value": "yes"},
                        {"op": "set-volume", "kind": "stream", "key": 10, "value": 60}):
            with self.subTest(message=message):
                self.assertFalse(self.service.handle(message)["ok"])
        self.assertFalse(self.backend.calls)

    def test_shared_state_changes_revision_only_when_content_changes(self):
        revision = self.service.state["revision"]
        self.service.publish()
        self.assertEqual(self.service.state["revision"], revision)
        self.service.handle({"op": "mute"})
        self.assertGreater(self.service.state["revision"], revision)
        self.assertIn("已静音", self.service.state["bar"])
        self.assertTrue(self.service.state["target"]["level"]["muted"])

    def test_old_panel_cannot_close_new_instance(self):
        with patch("smart_audio.service.subprocess.Popen") as popen:
            popen.return_value.pid = 222
            popen.return_value.poll.return_value = None
            self.service.handle({"op": "panel"})
            self.service.handle({"op": "panel-closed", "pid": 111})
            self.assertTrue(self.service.panel["open"])


class SocketIntegrationTests(ServiceCase):
    def setUp(self):
        super().setUp()
        self.thread = threading.Thread(target=self.service.run)
        self.thread.start()
        self.addCleanup(self.finish)

    def finish(self):
        self.service.stop()
        self.thread.join(timeout=5)
        self.assertFalse(self.thread.is_alive(), "service did not exit")

    def test_cli_round_trip_and_subscription_share_verified_state(self):
        subscriber = connect(self.paths.socket)
        self.addCleanup(subscriber.close)
        subscriber.sendall(encode({"op": "subscribe"}))
        reader = subscriber.makefile("r", encoding="utf-8")
        self.addCleanup(reader.close)
        initial = json.loads(reader.readline())["state"]
        result = request(self.paths.socket, {"op": "adjust", "delta": 5})
        update = json.loads(reader.readline())["state"]
        self.assertTrue(result["ok"])
        self.assertEqual(update, result["state"])
        self.assertEqual(update["target"]["level"]["volume"], 55)
        self.assertGreater(update["revision"], initial["revision"])
        env = dict(os.environ, DWM_AUDIO_RUNTIME_DIR=str(self.paths.runtime))
        status = subprocess.check_output([str(ROOT / "scripts/utils/sound/audioctl"), "status"], env=env, text=True)
        self.assertEqual(status.strip(), update["bar"])

    def test_structured_requests_can_arrive_in_several_chunks(self):
        client = connect(self.paths.socket)
        with client:
            client.sendall(b'{"op": "ad')
            client.sendall(b'just", "delta": -5}\n')
            response = json.loads(client.makefile("rb").readline())
        self.assertTrue(response["ok"])
        self.assertEqual(response["state"]["target"]["level"]["volume"], 45)

    def test_invalid_json_and_oversized_request_do_not_crash_service(self):
        for payload in (b'not json\n', b'[]\n', b'x' * (64 * 1024 + 1)):
            with self.subTest(size=len(payload)):
                with connect(self.paths.socket) as client:
                    client.sendall(payload)
                    response = json.loads(client.makefile("rb").readline())
                    self.assertFalse(response["ok"])
        self.assertTrue(request(self.paths.socket, {"op": "ping"})["ok"])

    def test_slow_unfinished_client_does_not_block_volume_keys(self):
        with connect(self.paths.socket) as slow:
            slow.sendall(b'{"op":')
            result = request(self.paths.socket, {"op": "mute"})
            self.assertTrue(result["ok"])
            self.assertTrue(result["state"]["target"]["level"]["muted"])

    def test_focus_and_audio_events_update_unified_target(self):
        self.backend.data = replace(self.backend.data, streams=(stream(), stream(20, "spotify", 200)))
        self.service.wake()
        self.monitor.set(Window(2, 200, ("spotify",)))
        # A command also captures focus immediately, independent of event order.
        response = request(self.paths.socket, {"op": "mute"})
        self.assertEqual(response["state"]["target"]["key"], "app:spotify")
        self.assertEqual(self.backend.calls[-1], ("mute", "stream", 20, True))

    def test_stop_returns_response_before_unlinking_socket(self):
        self.assertTrue(request(self.paths.socket, {"op": "stop"})["ok"])
        self.thread.join(timeout=5)
        self.assertFalse(self.thread.is_alive())
        self.assertFalse(self.paths.socket.exists())


class LauncherTests(unittest.TestCase):
    def test_legacy_shell_entrypoints_map_to_unified_controller(self):
        with tempfile.TemporaryDirectory() as directory:
            python = Path(directory) / "python"
            python.write_text('#!/bin/sh\nprintf "%s\\n" "$@"\n')
            python.chmod(0o755)
            vol = ROOT / "scripts/statusbar/vol.sh"
            for arguments, button, expected in (([], "1", ["mute", "--pointer"]),
                                               ([], "2", ["panel", "apps"]),
                                               ([], "3", ["panel", "devices"]),
                                               ([], "4", ["adjust", "+1", "--pointer"]),
                                               ([], "5", ["adjust", "-1", "--pointer"]),
                                               (["change", "0"], "", ["mute"]),
                                               (["device", "0"], "", ["mute", "--device"]),
                                               (["device", "+5"], "", ["adjust", "+5", "--device"])):
                with self.subTest(arguments=arguments, button=button):
                    env = dict(os.environ, DWM_AUDIO_PYTHON=str(python), BUTTON=button)
                    output = subprocess.check_output([str(vol), *arguments], text=True, env=env).splitlines()
                    self.assertEqual(output[1:], expected)
            env = dict(os.environ, DWM_AUDIO_PYTHON=str(python))
            output = subprocess.check_output([str(ROOT / "scripts/utils/sound/mixer.sh")], text=True, env=env)
            self.assertEqual(output.splitlines()[1:], ["panel", "devices"])

    def test_status_does_not_start_service_when_it_is_offline(self):
        with tempfile.TemporaryDirectory() as directory:
            runtime = Path(directory) / "runtime"
            env = dict(os.environ, DWM_AUDIO_RUNTIME_DIR=str(runtime))
            output = subprocess.check_output([str(ROOT / "scripts/utils/sound/audioctl"), "status"], text=True, env=env)
            self.assertIn("音频离线", output)
            self.assertFalse(runtime.exists())

    def test_daemon_singleton_does_not_load_native_dependencies_twice(self):
        with tempfile.TemporaryDirectory() as directory:
            paths = Paths(Path(directory), Path(directory) / "preferences.json")
            with paths.lock.open("w") as lock:
                fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
                with patch("smart_audio.service.PulseBackend") as backend:
                    self.assertEqual(run_daemon(paths), 0)
                    backend.assert_not_called()

    def test_client_rejects_foreign_uid_service_socket(self):
        client = Mock()
        client.getsockopt.return_value = struct.pack("3i", 123, os.getuid() + 1, 0)
        with patch("smart_audio.ipc.socket.socket", return_value=client):
            with self.assertRaisesRegex(IPCError, "不属于当前用户"):
                connect(Path("/not-used"))
        client.close.assert_called_once()

    def test_non_socket_stale_path_is_not_deleted(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "control.sock"
            path.write_text("user data")
            with selectors.DefaultSelector() as selector:
                with self.assertRaises(IPCError):
                    JsonServer(path, selector, lambda _x: {}, lambda: {})
            self.assertEqual(path.read_text(), "user data")


if __name__ == "__main__":
    unittest.main()

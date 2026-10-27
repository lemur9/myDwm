from __future__ import annotations

import tempfile
import unittest
from dataclasses import replace
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch

from audio_helpers import (BLUETOOTH, SPEAKERS, USB, FakeBackend, FakeProcesses,
                           Process, Snapshot, Window, stream)
from smart_audio.backend import sink_from_pulse, stream_from_pulse
from smart_audio.engine import CommandError, Engine
from smart_audio.identity import clean_text, identify
from smart_audio.model import Sink, Target, panel_geometry
from smart_audio.storage import Preferences


class EngineCase(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.preferences = Preferences(Path(self.temporary.name) / "state/preferences.json")
        self.backend = FakeBackend()
        self.notices = []
        self.engine = Engine(self.backend, self.preferences, FakeProcesses(),
                             lambda *args: self.notices.append(args))

    def load(self, *streams, sinks=None, default=None):
        self.backend.data = Snapshot(tuple(sinks) if sinks is not None else (SPEAKERS, USB, BLUETOOTH),
                                     tuple(streams), default if default is not None else SPEAKERS.name)
        self.engine.refresh()
        return self.engine

    def remember(self, sink=BLUETOOTH, key="app:mpv"):
        self.preferences.set(key, sink.name, sink.label, "mpv")


class TargetPolicyTests(EngineCase):
    def test_foreground_wins_over_background_music_for_volume_and_mute(self):
        self.load(stream(), stream(20, "spotify", 200))
        self.engine.set_window(Window(1, 100, ("mpv",)))
        self.engine.control("volume", delta=5)
        self.engine.control("mute")
        self.assertEqual(self.backend.calls, [("volume", "stream", 10, 55), ("mute", "stream", 10, True)])
        self.assertEqual(self.engine.view()["target"]["key"], "app:mpv")

    def test_one_background_app_is_selected(self):
        self.load(stream())
        self.engine.set_window(Window(2, 900, ("editor",)))
        self.assertEqual(self.engine.target.key, "app:mpv")

    def test_multiple_background_apps_do_not_change_any_volume_or_mute(self):
        self.load(stream(), stream(20, "spotify", 200))
        self.assertEqual(self.engine.control("volume", delta=5), "choose")
        self.assertEqual(self.engine.control("mute"), "choose")
        self.assertEqual(self.backend.calls, [])

    def test_multiple_streams_of_one_app_are_not_ambiguous(self):
        self.load(stream(), stream(11, pid=101))
        self.assertEqual(self.engine.target.kind, "app")
        self.assertEqual(len(self.engine.apps), 1)
        self.engine.control("volume", delta=-5)
        self.assertEqual([s.volume for s in self.backend.data.streams], [45, 45])

    def test_no_streams_controls_only_default_output(self):
        self.load()
        self.engine.control("mute")
        self.assertEqual(self.backend.calls, [("mute", "sink", 1, True)])

    def test_only_playing_background_apps_count(self):
        self.load(stream(corked=True), stream(20, "spotify", 200))
        self.assertEqual(self.engine.target.key, "app:spotify")
        self.load(stream(corked=True))
        self.assertEqual(self.engine.target.kind, "sink")

    def test_foreground_paused_app_still_controls_its_stream(self):
        self.load(stream(corked=True), stream(20, "spotify", 200))
        self.engine.set_window(Window(1, 100))
        self.assertEqual(self.engine.target.key, "app:mpv")

    def test_lock_overrides_focus_and_survives_stream_recreation(self):
        self.load(stream(), stream(20, "spotify", 200))
        self.engine.set_lock("app:mpv")
        self.engine.set_window(Window(2, 200))
        self.load(stream(99, pid=999), stream(20, "spotify", 200))
        self.engine.control("mute")
        self.assertEqual(self.backend.calls[-1], ("mute", "stream", 99, True))
        self.assertTrue(self.engine.target.locked)

    def test_unavailable_lock_never_falls_back_to_another_app_or_sink(self):
        self.load(stream())
        self.engine.set_lock("app:mpv")
        self.load(stream(20, "spotify", 200))
        with self.assertRaisesRegex(CommandError, "锁定应用"):
            self.engine.control("volume", delta=5)
        self.assertEqual(self.backend.calls, [])
        self.assertIn("󰌾", self.engine.view()["bar"])
        self.engine.unlock()
        self.assertEqual(self.engine.target.key, "app:spotify")

    def test_lock_is_not_saved_in_preferences_or_new_engine(self):
        self.load(stream())
        self.engine.set_lock("app:mpv")
        fresh = Engine(self.backend, self.preferences, FakeProcesses())
        fresh.refresh()
        self.assertIsNone(fresh.lock)
        self.assertFalse(self.preferences.path.exists())

    def test_panel_window_does_not_replace_original_context(self):
        self.load(stream(), stream(20, "spotify", 200))
        self.engine.set_window(Window(1, 100))
        self.engine.set_window(Window(100, 999, ("dwm-audio", "DwmAudio")))
        self.assertEqual(self.engine.window.pid, 100)
        self.assertEqual(self.engine.target.key, "app:mpv")

    def test_default_shortcut_bypasses_lock_without_modifying_it(self):
        self.load(stream())
        self.engine.set_lock("app:mpv")
        self.engine.control("volume", "default", delta=5)
        self.engine.control("mute", "default")
        self.assertEqual(self.backend.calls, [("volume", "sink", 1, 55), ("mute", "sink", 1, True)])
        self.assertEqual(self.engine.lock.key, "app:mpv")
        self.assertEqual(self.backend.data.streams[0].volume, 50)

    def test_no_output_retains_lock_and_feedback(self):
        self.load(stream())
        self.engine.set_lock("app:mpv")
        self.load(sinks=(), default="")
        self.assertTrue(self.engine.view()["waiting_for_output"])
        self.assertTrue(self.engine.target.locked)

    def test_read_only_background_stream_is_not_a_control_target(self):
        self.load(stream(writable=False))
        self.assertEqual(self.engine.target.kind, "sink")


class IdentityTests(EngineCase):
    def test_browser_sibling_processes_match_via_browser_ancestor(self):
        self.engine.processes = FakeProcesses(Process(50, 1, "google-chrome"),
                                               Process(100, 50, "google-chrome"),
                                               Process(300, 50, "google-chrome"))
        self.load(stream(10, "chrome", 300), stream(20, "spotify", 200))
        self.engine.set_window(Window(1, 100))
        self.assertEqual(self.engine.target.key, "app:google-chrome")

    def test_multilevel_audio_descendant_matches_window_pid(self):
        self.engine.processes = FakeProcesses(Process(100, 1, "chrome"),
                                               Process(200, 100, "helper"), Process(300, 200, "audio"))
        self.load(stream(10, "chrome", 300), stream(20, "spotify", 400))
        self.engine.set_window(Window(1, 100))
        self.assertEqual(self.engine.target.key, "app:google-chrome")

    def test_shared_shell_or_session_parent_is_not_an_app_match(self):
        self.engine.processes = FakeProcesses(Process(50, 1, "bash"), Process(100, 50, "editor"),
                                               Process(300, 50, "mpv"))
        self.load(stream(10, "mpv", 300), stream(20, "spotify", 200))
        self.engine.set_window(Window(1, 100, ("editor",)))
        self.assertEqual(self.engine.target.kind, "ambiguous")

    def test_exact_wm_class_fallback_without_pid(self):
        self.load(stream(10, "chrome", 300), stream(20, "spotify", 200))
        self.engine.set_window(Window(1, 0, ("google-chrome", "Google-chrome")))
        self.assertEqual(self.engine.target.key, "app:google-chrome")

    def test_known_desktop_id_and_binary_group_together(self):
        self.load(stream(10, "chrome", properties={"application.id": "com.google.Chrome"}),
                  stream(11, "google-chrome-stable", pid=999))
        self.assertEqual(len(self.engine.apps), 1)
        self.assertEqual(len(self.engine.view()["apps"][0]["streams"]), 2)

    def test_chromium_and_chrome_do_not_group_together(self):
        self.load(stream(10, "chrome"), stream(11, "chromium", pid=999))
        self.assertEqual(len(self.engine.apps), 2)

    def test_media_title_is_displayed_but_never_guessed_as_a_window_match(self):
        self.load(stream(properties={"media.name": "Chrome tab title"}), stream(20, "spotify", 200))
        self.engine.set_window(Window(1, 999, ("Editor",)))
        self.assertEqual(self.engine.target.kind, "ambiguous")
        self.assertEqual(self.engine.view()["apps"][0]["streams"][0]["media"], "Chrome tab title")

    def test_identity_does_not_depend_on_pid_or_stream_index(self):
        a = identify(stream().properties, 100, FakeProcesses())
        b = identify(stream(99, pid=900).properties, 900, FakeProcesses())
        self.assertEqual(a.key, b.key)

    def test_untrusted_status_control_bytes_are_removed(self):
        self.load(stream(properties={"application.name": "evil\x06app\nfoo\x00"}))
        self.assertNotIn("\x06", self.engine.view()["bar"])
        self.assertEqual(clean_text("x\x01y\u202ez"), "xyz")


    def test_name_only_known_browser_stream_groups_with_binary_stream(self):
        self.load(stream(10, "chrome"), stream(11, "chrome", pid=999,
                  properties={"application.process.binary": "", "application.name": "Google Chrome"}))
        self.assertEqual(len(self.engine.apps), 1)

    def test_plain_application_id_is_stable_when_binary_metadata_is_missing(self):
        first = identify({"application.id": "my-player", "application.process.binary": "my-player"}, 0, FakeProcesses())
        second = identify({"application.id": "my-player"}, 0, FakeProcesses())
        self.assertEqual(first.key, second.key)

    def test_terminal_does_not_claim_audio_from_background_children(self):
        self.engine.processes = FakeProcesses(Process(100, 1, "st"), Process(200, 100, "bash"),
                                               Process(300, 200, "mpv"), Process(400, 200, "spotify"))
        self.load(stream(10, "mpv", 300), stream(20, "spotify", 400))
        self.engine.set_window(Window(1, 100, ("st",)))
        self.assertEqual(self.engine.target.kind, "ambiguous")


class VolumeTests(EngineCase):
    def test_volume_is_bounded_and_does_not_unmute(self):
        self.load(stream(volume=99, muted=True))
        self.engine.control("volume", delta=5)
        self.assertEqual(self.backend.data.streams[0].volume, 100)
        self.assertTrue(self.backend.data.streams[0].muted)
        self.engine.control("volume", delta=-100)
        self.engine.control("volume", delta=-5)
        self.assertEqual(self.backend.data.streams[0].volume, 0)
        self.assertTrue(self.backend.data.streams[0].muted)

    def test_zero_volume_is_not_mute(self):
        self.load(stream(volume=0))
        self.assertFalse(self.engine.view()["target"]["level"]["muted"])
        self.assertIn("", self.engine.view()["bar"])
        self.assertIn("0%", self.engine.view()["bar"])
        self.assertNotIn("已静音", self.engine.view()["bar"])

    def test_mixed_streams_are_presented_as_a_range_and_partial_mute(self):
        self.load(stream(volume=25), stream(11, volume=75, muted=True))
        app = self.engine.view()["apps"][0]
        self.assertEqual(app["level"]["text"], "25–75%")
        self.assertIsNone(app["level"]["muted"])
        self.assertIn("部分静音", self.engine.view()["bar"])
        self.engine.control("volume", delta=5)
        self.assertEqual([s.volume for s in self.backend.data.streams], [30, 80])

    def test_mixed_mute_toggle_mutes_all_then_unmutes_all(self):
        self.load(stream(), stream(11, muted=True))
        self.engine.control("mute")
        self.assertTrue(all(s.muted for s in self.backend.data.streams))
        self.engine.control("mute")
        self.assertTrue(all(not s.muted for s in self.backend.data.streams))

    def test_expanded_stream_control_does_not_modify_sibling(self):
        self.load(stream(), stream(11))
        self.engine.control("volume", value=64, target=Target("stream", "11", "app:mpv"))
        self.assertEqual([s.volume for s in self.backend.data.streams], [50, 64])

    def test_stale_stream_owner_is_rejected(self):
        self.load(stream(11, "spotify", 200))
        with self.assertRaises(CommandError):
            self.engine.control("mute", target=Target("stream", "11", "app:mpv"))
        self.assertFalse(self.backend.calls)

    def test_no_op_backend_is_not_reported_as_success(self):
        self.load(stream())
        self.backend.no_op = True
        with self.assertRaisesRegex(CommandError, "未按请求更新"):
            self.engine.control("volume", delta=5)
        self.assertEqual(self.engine.view()["target"]["level"]["volume"], 50)
        self.assertEqual(self.notices, [])

    def test_partial_failure_reads_actual_state_without_success_notice(self):
        self.load(stream(), stream(11))
        self.backend.fail_volume.add(11)
        with self.assertRaisesRegex(CommandError, "写入失败"):
            self.engine.control("volume", delta=5)
        self.assertEqual(self.engine.view()["apps"][0]["level"]["text"], "50–55%")
        self.assertEqual(self.notices, [])

    def test_disconnection_and_reconnection_preserve_lock(self):
        self.load(stream())
        self.engine.set_lock("app:mpv")
        self.backend.online = False
        self.engine.refresh()
        self.assertFalse(self.engine.view()["connected"])
        self.assertEqual(self.engine.view()["apps"], [])
        with self.assertRaises(CommandError):
            self.engine.control("mute")
        self.backend.online = True
        self.load(stream(90, pid=999))
        self.assertEqual(self.engine.target.kind, "app")
        self.assertEqual(self.engine.target.key, "app:mpv")
        self.assertTrue(self.engine.target.locked)

    def test_channel_differences_remain_visible_in_shared_state(self):
        self.load(replace(stream(), channels=(0.2, 0.8)))
        self.assertTrue(self.engine.view()["apps"][0]["level"]["channel_mixed"])
        self.assertEqual(self.engine.view()["apps"][0]["streams"][0]["channel_values"], [20, 80])


class RoutingTests(EngineCase):
    def test_manual_route_is_persistent_and_new_streams_use_it(self):
        self.load(stream())
        self.engine.route("app:mpv", USB.name)
        self.assertEqual(self.backend.data.streams[0].sink, USB.index)
        saved = Preferences(self.preferences.path)
        self.assertEqual(saved.apps["app:mpv"]["sink"], USB.name)
        fresh = Engine(self.backend, saved, FakeProcesses())
        self.backend.data = replace(self.backend.data, streams=(stream(90, pid=999),))
        fresh.refresh()
        self.assertEqual(self.backend.data.streams[0].sink, USB.index)

    def test_disconnect_falls_back_to_default_and_preserves_preference(self):
        self.remember()
        self.load(stream(sink=3))
        self.load(stream(sink=3), sinks=(SPEAKERS, USB))
        self.assertEqual(self.backend.data.streams[0].sink, 1)
        self.assertEqual(self.preferences.apps["app:mpv"]["sink"], BLUETOOTH.name)
        self.assertIn("蓝牙耳机", self.notices[-1][0])
        self.assertIn("内置扬声器", self.notices[-1][0])
        self.assertIn("已切换", self.notices[-1][0])

    def test_reconnect_recovers_using_stable_name_not_new_index(self):
        self.remember()
        self.load(stream(sink=3))
        self.load(stream(sink=3), sinks=(SPEAKERS, USB))
        replacement = replace(BLUETOOTH, index=99)
        self.load(*self.backend.data.streams, sinks=(SPEAKERS, USB, replacement))
        self.assertEqual(self.backend.data.streams[0].sink, 99)
        self.assertIn("恢复，已切回", self.notices[-1][0])

    def test_fallback_is_stably_sorted_if_default_is_unavailable(self):
        self.remember()
        a, z = Sink(7, "a", "A"), Sink(8, "z", "Z")
        self.load(stream(sink=3), sinks=(z, a), default="gone")
        self.assertEqual(self.backend.data.streams[0].sink, a.index)

    def test_suspended_device_is_not_disconnected(self):
        self.remember()
        self.load(stream(sink=3), sinks=(SPEAKERS, USB, replace(BLUETOOTH, state="suspended")))
        self.assertEqual(self.backend.calls, [])
        self.assertEqual(self.notices, [])

    def test_explicit_port_unavailability_triggers_fallback(self):
        self.remember()
        self.load(stream(sink=3), sinks=(SPEAKERS, USB, replace(BLUETOOTH, available=False)))
        self.assertEqual(self.backend.data.streams[0].sink, 1)

    def test_manual_change_during_fallback_overrides_old_preference(self):
        self.remember()
        self.load(stream(sink=3), sinks=(SPEAKERS, USB))
        self.engine.route("app:mpv", USB.name)
        self.load(*self.backend.data.streams)
        self.assertEqual(self.backend.data.streams[0].sink, USB.index)
        self.assertEqual(self.preferences.apps["app:mpv"]["sink"], USB.name)

    def test_all_outputs_gone_then_recover(self):
        self.remember()
        self.load(stream(sink=3), sinks=(), default="")
        self.engine.set_lock("app:mpv")
        self.assertTrue(self.engine.view()["waiting_for_output"])
        self.assertFalse(self.backend.calls)
        self.load(stream(sink=3), sinks=(USB,), default=USB.name)
        self.assertEqual(self.backend.data.streams[0].sink, USB.index)
        self.load(*self.backend.data.streams, sinks=(USB, BLUETOOTH), default=USB.name)
        self.assertEqual(self.backend.data.streams[0].sink, BLUETOOTH.index)
        self.assertTrue(self.engine.target.locked)

    def test_default_change_does_not_sweep_apps_with_or_without_preferences(self):
        self.remember(USB)
        self.load(stream(sink=2), stream(20, "spotify", 200, sink=1))
        self.backend.calls.clear()
        self.engine.set_default(BLUETOOTH.name)
        self.assertEqual(self.backend.calls, [("default", BLUETOOTH.name)])
        self.assertEqual([s.sink for s in self.backend.data.streams], [2, 1])

    def test_route_failure_keeps_latest_intent_but_not_false_success(self):
        self.load(stream())
        self.backend.fail_moves.add(10)
        with self.assertRaisesRegex(CommandError, "迁移失败"):
            self.engine.route("app:mpv", USB.name)
        self.assertEqual(self.preferences.apps["app:mpv"]["sink"], USB.name)
        self.assertEqual(self.engine.view()["apps"][0]["outputs"][0]["name"], SPEAKERS.name)
        self.assertTrue(all("已记住该应用" not in n[1] for n in self.notices))
        self.backend.fail_moves.clear()
        self.engine.refresh()
        self.assertEqual(self.backend.data.streams[0].sink, USB.index)
        self.assertFalse(self.engine.routing_errors)

    def test_duplicate_fallback_events_do_not_spam_notifications(self):
        self.remember()
        self.load(stream(sink=3), sinks=(SPEAKERS, USB))
        self.engine.refresh()
        self.engine.refresh()
        self.assertEqual(len(self.notices), 1)

    def test_forget_cancels_preference_and_only_resets_this_app(self):
        self.remember(USB)
        self.load(stream(sink=2), stream(20, "spotify", 200, sink=3))
        self.backend.calls.clear()
        self.engine.forget("app:mpv")
        self.assertNotIn("app:mpv", self.preferences.apps)
        self.assertEqual([s.sink for s in self.backend.data.streams], [1, 3])
        self.assertIn(("forget-restore", (10,)), self.backend.calls)

    def test_unpreferred_app_on_disconnected_output_also_falls_back(self):
        self.load(stream(sink=99), sinks=(SPEAKERS,))
        self.assertEqual(self.backend.data.streams[0].sink, SPEAKERS.index)
        self.assertFalse(self.preferences.apps)

    def test_routing_no_op_is_not_success(self):
        self.load(stream())
        self.backend.no_op = True
        with self.assertRaisesRegex(CommandError, "未能迁移"):
            self.engine.route("app:mpv", USB.name)

    def test_set_default_no_op_is_not_success(self):
        self.load()
        self.backend.no_op = True
        with self.assertRaisesRegex(CommandError, "未按请求更新"):
            self.engine.set_default(USB.name)


    def test_unpreferred_multistream_fallback_preserves_other_healthy_routes(self):
        self.load(stream(sink=99), stream(11, sink=USB.index))
        self.assertEqual([s.sink for s in self.backend.data.streams], [SPEAKERS.index, USB.index])
        self.assertFalse(self.engine.routing_errors)
        self.assertIn("已切换", self.notices[-1][0])

    def test_device_disappearing_during_move_is_reported_as_failure(self):
        self.load(stream())
        move = self.backend.move

        def unplug(index, sink_index):
            move(index, sink_index)
            self.backend.data = replace(self.backend.data, sinks=(SPEAKERS,))
        with patch.object(self.backend, "move", side_effect=unplug):
            with self.assertRaisesRegex(CommandError, "不可用"):
                self.engine.route("app:mpv", USB.name)
        self.assertTrue(all("已记住该应用" not in n[1] for n in self.notices))

    def test_forget_does_not_claim_current_route_was_reset_if_move_was_no_op(self):
        self.remember(USB)
        self.load(stream(sink=USB.index))
        self.backend.no_op = True
        with self.assertRaisesRegex(CommandError, "未能回到默认输出"):
            self.engine.forget("app:mpv")
        self.assertNotIn("app:mpv", self.preferences.apps)



class PulseConversionTests(unittest.TestCase):
    def test_suspended_sink_with_unknown_port_remains_available(self):
        item = SimpleNamespace(index=3, name="bt", description="耳机", state=SimpleNamespace(_value="suspended"),
                               port_active=SimpleNamespace(available=SimpleNamespace(_value="unknown")),
                               volume=SimpleNamespace(values=[0.5, 0.5]), mute=False)
        self.assertTrue(sink_from_pulse(item).available)
        item.port_active.available._value = "no"
        self.assertFalse(sink_from_pulse(item).available)

    def test_stream_volume_writability_is_respected(self):
        item = SimpleNamespace(index=1, sink=2, proplist={}, volume=SimpleNamespace(values=[0.5]),
                               mute=False, corked=False, has_volume=True, volume_writable=False,
                               sample_spec=SimpleNamespace(rate=48000))
        self.assertFalse(stream_from_pulse(item).writable)


class GeometryTests(unittest.TestCase):
    def test_negative_monitor_origin(self):
        x, y, width, height = panel_geometry((-1920, 30, 1920, 1050), (-20, 40), "pointer")
        self.assertGreaterEqual(x, -1920)
        self.assertGreaterEqual(y, 30)
        self.assertLessEqual(x + width, 0)
        self.assertLessEqual(y + height, 1080)

    def test_pointer_at_bottom_right_does_not_overflow(self):
        x, y, width, height = panel_geometry((1920, 0, 1280, 720), (3199, 719), "pointer")
        self.assertGreaterEqual(x, 1920)
        self.assertLessEqual(x + width, 3200)
        self.assertLessEqual(y + height, 720)

    def test_small_workarea_shrinks_window(self):
        x, y, width, height = panel_geometry((0, 0, 500, 400), (250, 200), "window")
        self.assertEqual((width, height), (476, 376))
        self.assertEqual((x, y), (12, 12))


if __name__ == "__main__":
    unittest.main()

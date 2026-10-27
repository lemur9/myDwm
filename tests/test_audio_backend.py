"""Validate libpulse adapter calls without requiring native libpulse in CI."""
from __future__ import annotations

import unittest
from types import SimpleNamespace
from unittest.mock import Mock, patch

import audio_helpers  # noqa: F401 - configure the source tree import path
from smart_audio.backend import AudioUnavailable, PulseBackend, PulseEvents, load_pulse
from smart_audio.model import Stream


class PulseFailure(Exception):
    pass


def pulse_module(*connections):
    return SimpleNamespace(Pulse=Mock(side_effect=connections), PulseError=PulseFailure,
                           PulseDisconnected=PulseFailure)


class BackendTests(unittest.TestCase):
    def test_volume_writes_are_bounded_without_touching_mute(self):
        connection = Mock(connected=True)
        module = pulse_module(connection)
        with patch("smart_audio.backend.load_pulse", return_value=module):
            backend = PulseBackend()
            backend.set_volume("stream", 10, 180)
            connection.volume_set_all_chans.assert_called_with(connection.sink_input_info.return_value, 1.0)
            backend.set_volume("sink", 2, -10)
            connection.volume_set_all_chans.assert_called_with(connection.sink_info.return_value, 0.0)
            connection.sink_mute.assert_not_called()
            connection.sink_input_mute.assert_not_called()
            connection.connect.assert_called_once_with(autospawn=False, timeout=1.5)
            backend.close()
            connection.close.assert_called_once()

    def test_failed_connection_is_closed_and_later_recreated(self):
        failed, healthy = Mock(connected=False), Mock(connected=True)
        failed.connect.side_effect = PulseFailure("offline")
        healthy.server_info.return_value.default_sink_name = "sink"
        healthy.sink_list.return_value = []
        healthy.sink_input_list.return_value = []
        with patch("smart_audio.backend.load_pulse", return_value=pulse_module(failed, healthy)):
            backend = PulseBackend()
            with self.assertRaises(AudioUnavailable):
                backend.snapshot()
            failed.close.assert_called_once()
            self.assertEqual(backend.snapshot().default_sink, "sink")
            self.assertEqual(healthy.connect.call_args.kwargs["autospawn"], False)
            backend.close()

    def test_disconnect_during_snapshot_discards_connection_then_recovers(self):
        lost, healthy = Mock(connected=True), Mock(connected=True)

        def disconnect():
            lost.connected = False
            raise PulseFailure("connection reset")
        lost.server_info.side_effect = disconnect
        healthy.server_info.return_value.default_sink_name = "new"
        healthy.sink_list.return_value = []
        healthy.sink_input_list.return_value = []
        with patch("smart_audio.backend.load_pulse", return_value=pulse_module(lost, healthy)):
            backend = PulseBackend()
            with self.assertRaises(AudioUnavailable):
                backend.snapshot()
            self.assertIsNone(backend.pulse)
            self.assertEqual(backend.snapshot().default_sink, "new")
            backend.close()

    def test_forgetting_restore_changes_only_matching_devices_not_volume_or_other_apps(self):
        volume = object()
        matching = SimpleNamespace(name="sink-input-by-application-name:mpv", device="usb", volume=volume, mute=True)
        other = SimpleNamespace(name="sink-input-by-application-name:spotify", device="bt", volume=volume, mute=False)
        connection = Mock(connected=True)
        connection.stream_restore_list.return_value = [matching, other]
        with patch("smart_audio.backend.load_pulse", return_value=pulse_module(connection)):
            backend = PulseBackend()
            streams = [Stream(10, 1, {"module-stream-restore.id": matching.name})]
            backend.forget_restore_devices(streams)
            self.assertIsNone(matching.device)
            self.assertIs(matching.volume, volume)
            self.assertTrue(matching.mute)
            self.assertEqual(other.device, "bt")
            connection.stream_restore_write.assert_called_once_with(matching, mode="replace", apply_immediately=False)
            connection.stream_restore_delete.assert_not_called()
            backend.close()

    def test_event_connection_only_wakes_service_and_never_changes_audio(self):
        connection = Mock()
        changed = Mock()
        events = PulseEvents(changed)

        def listen(**_kwargs):
            connection.event_callback_set.call_args.args[0](object())
            events.stop()
        connection.event_listen.side_effect = listen
        with patch("smart_audio.backend.load_pulse", return_value=pulse_module(connection)):
            events.run()
        self.assertEqual(changed.call_count, 2)  # initial resync + event callback
        connection.connect.assert_called_once_with(autospawn=False, timeout=1.5)
        connection.event_mask_set.assert_called_once_with("sink", "sink_input", "server", "card")
        connection.sink_input_move.assert_not_called()
        connection.close.assert_called_once()

    def test_volume_capability_fields_are_added_idempotently_to_decoder(self):
        module = SimpleNamespace(PulseSinkInputInfo=type("Info", (), {"c_struct_fields": "index name"}))
        with patch.dict("sys.modules", {"pulsectl": module}):
            self.assertIs(load_pulse(), module)
            load_pulse()
            fields = module.PulseSinkInputInfo.c_struct_fields
            self.assertEqual(fields.count("has_volume"), 1)
            self.assertEqual(fields.count("volume_writable"), 1)


if __name__ == "__main__":
    unittest.main()

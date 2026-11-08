import 'package:audioplayers/audioplayers.dart';
import 'package:flutter/services.dart';

/// Plays a short audible alert (plus a haptic buzz) when a new detection is
/// flagged on the Detect screen. Kept as a tiny singleton so a single
/// AudioPlayer is reused and the bundled tone asset is only loaded once.
///
/// Fire-and-forget and fully guarded: if audio is unavailable (emulator,
/// no output device, platform quirk) it silently falls back to just the
/// haptic, so a detection is never missed because sound failed.
class AlertSound {
  AlertSound._();
  static final AlertSound instance = AlertSound._();

  final AudioPlayer _player = AudioPlayer(playerId: 'detect_alert');
  bool _ready = false;

  Future<void> _ensure() async {
    if (_ready) return;
    try {
      // Low-latency mode so the beep fires promptly on each detection.
      await _player.setReleaseMode(ReleaseMode.stop);
      await _player.setPlayerMode(PlayerMode.lowLatency);
      await _player.setSource(AssetSource('sounds/detect_alert.wav'));
      _ready = true;
    } catch (_) {
      _ready = false; // try again next time
    }
  }

  /// Play the alert once. [haptic] adds a buzz; [sound] plays the tone.
  Future<void> fire({bool sound = true, bool haptic = true}) async {
    if (haptic) {
      try {
        await HapticFeedback.heavyImpact();
      } catch (_) {/* ignore */}
    }
    if (!sound) return;
    try {
      await _ensure();
      await _player.stop();
      await _player.resume();
    } catch (_) {/* audio unavailable — haptic already fired */}
  }

  Future<void> dispose() async {
    try {
      await _player.dispose();
    } catch (_) {/* ignore */}
  }
}

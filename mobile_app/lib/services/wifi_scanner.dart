import 'dart:async';
import 'package:flutter/foundation.dart';
import 'package:wifi_scan/wifi_scan.dart';

/// A scanned WiFi access point, reduced to what the detector needs.
class WifiAp {
  final String ssid;
  final String bssid;
  final int rssi;
  const WifiAp({required this.ssid, required this.bssid, required this.rssi});
}

/// Periodically scans nearby WiFi access points and streams them, so the
/// detection engine can flag WiFi-only gear — Flock cameras, ALPR, Ring, body
/// cams — that a Bluetooth-only scan can never see.
///
/// Android throttles WiFi scans to ~4 per 2 minutes for foreground apps, so
/// results refresh slower than BLE: we trigger a scan on an interval and also
/// serve the last cached results. Unsupported on iOS (no public scan API) — the
/// scanner just stays quiet there.
class WifiScanner {
  final _controller = StreamController<List<WifiAp>>.broadcast();
  Stream<List<WifiAp>> get results => _controller.stream;

  Timer? _timer;
  StreamSubscription<List<WiFiAccessPoint>>? _sub;
  bool _running = false;

  /// Whether this platform can return scan results at all (false on iOS).
  Future<bool> isSupported() async {
    try {
      final can =
          await WiFiScan.instance.canGetScannedResults(askPermissions: false);
      return can != CanGetScannedResults.notSupported;
    } catch (_) {
      return false;
    }
  }

  /// Start scanning. Requests the needed permissions on the first call.
  Future<void> start() async {
    if (_running) return;
    _running = true;
    try {
      _sub = WiFiScan.instance.onScannedResultsAvailable
          .listen(_emit, onError: (_) {});
    } catch (_) {/* stream unsupported on this platform */}
    await _scanOnce(ask: true);
    _timer = Timer.periodic(const Duration(seconds: 25), (_) => _scanOnce());
  }

  Future<void> _scanOnce({bool ask = false}) async {
    if (!_running) return;
    try {
      final canStart =
          await WiFiScan.instance.canStartScan(askPermissions: ask);
      if (canStart == CanStartScan.yes) {
        await WiFiScan.instance.startScan();
      }
      final canGet =
          await WiFiScan.instance.canGetScannedResults(askPermissions: ask);
      if (canGet == CanGetScannedResults.yes) {
        _emit(await WiFiScan.instance.getScannedResults());
      }
    } catch (e) {
      if (kDebugMode) debugPrint('WiFi scan error: $e');
    }
  }

  void _emit(List<WiFiAccessPoint> aps) {
    if (_controller.isClosed) return;
    _controller.add(<WifiAp>[
      for (final a in aps) WifiAp(ssid: a.ssid, bssid: a.bssid, rssi: a.level),
    ]);
  }

  Future<void> stop() async {
    _running = false;
    _timer?.cancel();
    _timer = null;
    await _sub?.cancel();
    _sub = null;
  }

  void dispose() {
    stop();
    _controller.close();
  }
}

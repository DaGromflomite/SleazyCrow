import 'dart:async';
import 'package:flutter/services.dart';

/// A classic-Bluetooth (BR/EDR) device found by inquiry. Separate from BLE's
/// ScanResult — classic discovery gives a coarse, bursty RSSI and a device class.
class ClassicBtDevice {
  final String address;
  final String? name;
  final int rssi;
  final int deviceClass;
  final int majorClass;

  ClassicBtDevice({
    required this.address,
    required this.name,
    required this.rssi,
    required this.deviceClass,
    required this.majorClass,
  });

  factory ClassicBtDevice.fromMap(Map<dynamic, dynamic> m) => ClassicBtDevice(
        address: (m['address'] as String?) ?? '??',
        name: m['name'] as String?,
        rssi: (m['rssi'] as int?) ?? -127,
        deviceClass: (m['deviceClass'] as int?) ?? 0,
        majorClass: (m['majorClass'] as int?) ?? 0,
      );
}

/// Thin wrapper over the native classic-Bluetooth inquiry (MainActivity.kt).
/// Inquiry is slow (~12s cycles) and RSSI is coarse — fine for skimmer / gear
/// spotting, not for a tight fox hunt.
class ClassicBt {
  static const _method = MethodChannel('sleazycrow/classic_bt');

  /// While true, the Detect screen won't restart discovery after a cycle ends.
  /// Set during a Crow connect (discovery blocks BLE connects) and a fox hunt
  /// (discovery degrades BLE RSSI).
  static bool paused = false;
  static const _events = EventChannel('sleazycrow/classic_bt/events');

  static Future<bool> isSupported() async {
    try {
      return (await _method.invokeMethod<bool>('isSupported')) ?? false;
    } catch (_) {
      return false;
    }
  }

  static Future<bool> startDiscovery() async {
    try {
      return (await _method.invokeMethod<bool>('startDiscovery')) ?? false;
    } catch (_) {
      return false;
    }
  }

  static Future<void> stopDiscovery() async {
    try {
      await _method.invokeMethod('stopDiscovery');
    } catch (_) {/* ignore */}
  }

  /// Stream of found devices. The native 'finished' marker (one inquiry cycle
  /// ended) is surfaced as a null so callers can re-arm discovery if they want
  /// continuous scanning.
  static Stream<ClassicBtDevice?> events() {
    return _events.receiveBroadcastStream().map((e) {
      if (e is Map && e['event'] == 'finished') return null;
      return ClassicBtDevice.fromMap(e as Map<dynamic, dynamic>);
    });
  }
}

import 'dart:async';
import 'dart:convert';
import 'package:flutter/foundation.dart';
import 'package:shared_preferences/shared_preferences.dart';
import 'ble_provider.dart';

/// A TPMS sensor seen in the current sniffing session.
class TpmsSensor {
  final int id; // 32-bit fingerprint
  DateTime firstSeen;
  DateTime lastSeen;
  int? pressureKpa;
  int? temperatureC;
  int rssi;
  int protocol; // 0=generic, 1=Ford, 2=Toyota
  int tripsSeen; // distinct past sessions this id appeared in (incl. current)

  TpmsSensor(this.id, this.firstSeen, this.lastSeen, this.rssi,
      {this.pressureKpa,
      this.temperatureC,
      this.protocol = 0,
      this.tripsSeen = 1});

  String get hex => '0x${id.toRadixString(16).toUpperCase().padLeft(8, '0')}';
}

/// Owns the TPMS stalking-detection logic.
///
/// A "session" is one start()->stop() sniff (a drive/walk). Each session records
/// which sensor IDs it saw and when. A sensor ID that recurs across SEVERAL
/// separate sessions is a candidate follower — the same vehicle keeps showing up
/// near you. History is persisted so detection works across app restarts.
///
/// NOTE: the firmware's ID fingerprint + reception still need validating against
/// a real vehicle. `packets` (raw bursts captured) is the reception-confidence
/// signal: if it climbs near a moving car, the Crow is hearing TPMS.
class TpmsProvider extends ChangeNotifier {
  final BleProvider ble;
  StreamSubscription? _sub;

  TpmsProvider(this.ble) {
    _sub = ble.tpmsEvents.listen(_onEvent);
    _loadHistory();
  }

  static const _prefsKey = 'tpms_history_v1';
  // A sensor seen in at least this many distinct sessions is flagged.
  static const int followerThreshold = 3;

  bool scanning = false;
  bool active = false;
  int packets = 0;
  int seen = 0;
  double freqMHz = 315.0;

  final Map<int, TpmsSensor> _session = {}; // ids this session
  // Persistent: sensorId -> sorted list of session-start epoch millis it appeared in.
  final Map<int, List<int>> _history = {};
  DateTime? _sessionStart;

  List<TpmsSensor> get liveSensors {
    final list = _session.values.toList();
    list.sort((a, b) {
      final fa = isFollower(a.id), fb = isFollower(b.id);
      if (fa != fb) return fa ? -1 : 1; // followers first
      return b.lastSeen.compareTo(a.lastSeen);
    });
    return list;
  }

  int tripsFor(int id) => (_history[id] ?? const []).length;
  bool isFollower(int id) => tripsFor(id) >= followerThreshold;

  /// All sensor IDs ever flagged as followers (across history), most-seen first.
  List<int> get knownFollowers {
    final f = _history.keys.where(isFollower).toList();
    f.sort((a, b) => tripsFor(b).compareTo(tripsFor(a)));
    return f;
  }

  Future<void> start({int freqX100 = 31500}) async {
    if (!ble.isConnected) return;
    _session.clear();
    packets = 0;
    seen = 0;
    _sessionStart = DateTime.now();
    freqMHz = freqX100 / 100.0;
    scanning = true;
    notifyListeners();
    await ble.startTpmsScan(freqX100: freqX100);
  }

  Future<void> stop() async {
    scanning = false;
    notifyListeners();
    try {
      await ble.stopTpmsScan();
    } catch (_) {/* ignore */}
  }

  void _onEvent(Map<String, dynamic> ev) {
    final type = ev['type'];
    final d = (ev['data'] as Map?) ?? const {};
    if (type == 'TpmsStatus') {
      active = d['active'] == true;
      packets = (d['packets'] ?? packets) is int ? d['packets'] as int : packets;
      seen = (d['seen'] ?? seen) is int ? d['seen'] as int : seen;
      final f = d['freqMHz'];
      if (f is num) freqMHz = f.toDouble();
      notifyListeners();
    } else if (type == 'TpmsHit') {
      final id = d['sensorId'] as int?;
      if (id == null) return;
      _recordHit(
        id,
        rssi: (d['rssi'] ?? -50) is int ? d['rssi'] as int : -50,
        pressureKpa: d['pressureKpa'] as int?,
        temperatureC: d['temperatureC'] as int?,
        protocol: (d['protocol'] ?? 0) is int ? d['protocol'] as int : 0,
      );
    }
  }

  void _recordHit(int id,
      {required int rssi, int? pressureKpa, int? temperatureC, int protocol = 0}) {
    final now = DateTime.now();
    final existing = _session[id];
    if (existing == null) {
      // First time this id appears in THIS session -> record it in history.
      _appendSessionToHistory(id);
      _session[id] = TpmsSensor(id, now, now, rssi,
          pressureKpa: pressureKpa,
          temperatureC: temperatureC,
          protocol: protocol,
          tripsSeen: tripsFor(id));
      _saveHistory();
    } else {
      existing.lastSeen = now;
      existing.rssi = rssi;
      if (pressureKpa != null) existing.pressureKpa = pressureKpa;
      if (temperatureC != null) existing.temperatureC = temperatureC;
      if (protocol != 0) existing.protocol = protocol;
    }
    notifyListeners();
  }

  void _appendSessionToHistory(int id) {
    final start = (_sessionStart ?? DateTime.now()).millisecondsSinceEpoch;
    final list = _history.putIfAbsent(id, () => <int>[]);
    if (!list.contains(start)) list.add(start);
  }

  /// Wipe all remembered sensors/trips (so past followers no longer flag).
  Future<void> clearHistory() async {
    _history.clear();
    _session.clear();
    final prefs = await SharedPreferences.getInstance();
    await prefs.remove(_prefsKey);
    notifyListeners();
  }

  Future<void> _loadHistory() async {
    try {
      final prefs = await SharedPreferences.getInstance();
      final raw = prefs.getString(_prefsKey);
      if (raw != null) {
        final m = jsonDecode(raw) as Map<String, dynamic>;
        _history.clear();
        m.forEach((k, v) {
          final id = int.tryParse(k);
          if (id != null && v is List) {
            _history[id] = v.map((e) => (e as num).toInt()).toList();
          }
        });
        notifyListeners();
      }
    } catch (_) {/* ignore bad/absent cache */}
  }

  Future<void> _saveHistory() async {
    try {
      final prefs = await SharedPreferences.getInstance();
      final m = _history.map((k, v) => MapEntry(k.toString(), v));
      await prefs.setString(_prefsKey, jsonEncode(m));
    } catch (_) {/* ignore */}
  }

  @override
  void dispose() {
    _sub?.cancel();
    super.dispose();
  }
}

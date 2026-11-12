import 'dart:async';
import 'dart:convert';
import 'package:flutter/foundation.dart';
import 'package:shared_preferences/shared_preferences.dart';
import 'ble_provider.dart';

/// How aggressively to flag a sensor as a possible follower.
///  - immediate: flag any sensor the moment it's seen (best for testing, and for
///    "I was followed once" — one trip is still worth knowing).
///  - win30/60/120: flag a sensor seen 2+ times within 30 min / 1 hr / 2 hr.
///  - trips: flag a sensor seen across 3+ separate trips (strictest).
enum TpmsSensitivity { immediate, win30, win60, win120, trips }

extension TpmsSensitivityX on TpmsSensitivity {
  String get label => switch (this) {
        TpmsSensitivity.immediate => 'Any sensor — immediate',
        TpmsSensitivity.win30 => 'Seen 2+ times in 30 min',
        TpmsSensitivity.win60 => 'Seen 2+ times in 1 hour',
        TpmsSensitivity.win120 => 'Seen 2+ times in 2 hours',
        TpmsSensitivity.trips => 'Seen on 3+ separate trips',
      };
  String get chip => switch (this) {
        TpmsSensitivity.immediate => 'Immediate',
        TpmsSensitivity.win30 => '30 min',
        TpmsSensitivity.win60 => '1 hr',
        TpmsSensitivity.win120 => '2 hr',
        TpmsSensitivity.trips => '3 trips',
      };
  Duration? get window => switch (this) {
        TpmsSensitivity.win30 => const Duration(minutes: 30),
        TpmsSensitivity.win60 => const Duration(hours: 1),
        TpmsSensitivity.win120 => const Duration(hours: 2),
        _ => null,
      };
}

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
  static const _prefsSightings = 'tpms_sightings_v1';
  static const _prefsSensitivity = 'tpms_sensitivity_v1';
  // A sensor seen in at least this many distinct sessions is flagged (trips mode).
  static const int followerThreshold = 3;
  // For the time-window modes: how many sightings within the window = a follower.
  static const int _windowCount = 2;
  static const int _maxSightings = 80; // cap stored sighting times per sensor
  static const Duration _sightingTtl = Duration(hours: 6);

  /// How aggressively to flag followers. Default = immediate (so a single pass,
  /// and testing, light up right away).
  TpmsSensitivity sensitivity = TpmsSensitivity.immediate;

  bool scanning = false;
  bool active = false;
  int packets = 0;
  int seen = 0;
  double freqMHz = 315.0;

  final Map<int, TpmsSensor> _session = {}; // ids this session
  // Persistent: sensorId -> sorted list of session-start epoch millis it appeared in.
  final Map<int, List<int>> _history = {};
  // Persistent: sensorId -> recent detection epoch millis (for the window modes).
  final Map<int, List<int>> _sightings = {};
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

  /// Number of sightings of [id] within the current mode's time window.
  int sightingsInWindow(int id) {
    final w = sensitivity.window;
    if (w == null) return (_sightings[id] ?? const []).length;
    final cutoff =
        DateTime.now().millisecondsSinceEpoch - w.inMilliseconds;
    return (_sightings[id] ?? const []).where((t) => t >= cutoff).length;
  }

  /// Whether [id] is flagged as a possible follower under the current mode.
  bool isFollower(int id) {
    switch (sensitivity) {
      case TpmsSensitivity.immediate:
        return (_sightings[id]?.isNotEmpty ?? false);
      case TpmsSensitivity.trips:
        return tripsFor(id) >= followerThreshold;
      default:
        return sightingsInWindow(id) >= _windowCount;
    }
  }

  /// Short human reason a sensor is flagged, for the UI.
  String flagReason(int id) {
    switch (sensitivity) {
      case TpmsSensitivity.immediate:
        return 'detected';
      case TpmsSensitivity.trips:
        return 'seen on ${tripsFor(id)} trips';
      default:
        return 'seen ${sightingsInWindow(id)}× in ${sensitivity.chip}';
    }
  }

  Future<void> setSensitivity(TpmsSensitivity s) async {
    sensitivity = s;
    notifyListeners();
    try {
      final prefs = await SharedPreferences.getInstance();
      await prefs.setInt(_prefsSensitivity, s.index);
    } catch (_) {/* ignore */}
  }

  /// All sensor IDs currently flagged as followers, most-seen first.
  List<int> get knownFollowers {
    final ids = {..._history.keys, ..._sightings.keys};
    final f = ids.where(isFollower).toList();
    f.sort((a, b) => sightingsInWindow(b).compareTo(sightingsInWindow(a)));
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
    _recordSighting(id, now); // every detection (for the time-window modes)
    final existing = _session[id];
    if (existing == null) {
      // First time this id appears in THIS session -> record it in trip history.
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

  void _recordSighting(int id, DateTime now) {
    final ms = now.millisecondsSinceEpoch;
    final cutoff = ms - _sightingTtl.inMilliseconds;
    final list = _sightings.putIfAbsent(id, () => <int>[]);
    list.add(ms);
    list.removeWhere((t) => t < cutoff); // prune stale
    if (list.length > _maxSightings) {
      list.removeRange(0, list.length - _maxSightings);
    }
    _saveSightings();
  }

  void _appendSessionToHistory(int id) {
    final start = (_sessionStart ?? DateTime.now()).millisecondsSinceEpoch;
    final list = _history.putIfAbsent(id, () => <int>[]);
    if (!list.contains(start)) list.add(start);
  }

  /// Wipe all remembered sensors/trips/sightings (so past followers no longer flag).
  Future<void> clearHistory() async {
    _history.clear();
    _sightings.clear();
    _session.clear();
    final prefs = await SharedPreferences.getInstance();
    await prefs.remove(_prefsKey);
    await prefs.remove(_prefsSightings);
    notifyListeners();
  }

  Map<int, List<int>> _decodeMap(String? raw) {
    final out = <int, List<int>>{};
    if (raw == null) return out;
    try {
      (jsonDecode(raw) as Map<String, dynamic>).forEach((k, v) {
        final id = int.tryParse(k);
        if (id != null && v is List) {
          out[id] = v.map((e) => (e as num).toInt()).toList();
        }
      });
    } catch (_) {/* ignore */}
    return out;
  }

  Future<void> _loadHistory() async {
    try {
      final prefs = await SharedPreferences.getInstance();
      final si = prefs.getInt(_prefsSensitivity);
      if (si != null && si >= 0 && si < TpmsSensitivity.values.length) {
        sensitivity = TpmsSensitivity.values[si];
      }
      _history
        ..clear()
        ..addAll(_decodeMap(prefs.getString(_prefsKey)));
      _sightings
        ..clear()
        ..addAll(_decodeMap(prefs.getString(_prefsSightings)));
      notifyListeners();
    } catch (_) {/* ignore bad/absent cache */}
  }

  Future<void> _saveHistory() async {
    try {
      final prefs = await SharedPreferences.getInstance();
      await prefs.setString(_prefsKey,
          jsonEncode(_history.map((k, v) => MapEntry(k.toString(), v))));
    } catch (_) {/* ignore */}
  }

  Future<void> _saveSightings() async {
    try {
      final prefs = await SharedPreferences.getInstance();
      await prefs.setString(_prefsSightings,
          jsonEncode(_sightings.map((k, v) => MapEntry(k.toString(), v))));
    } catch (_) {/* ignore */}
  }

  @override
  void dispose() {
    _sub?.cancel();
    super.dispose();
  }
}

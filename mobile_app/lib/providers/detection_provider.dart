import 'dart:convert';
import 'package:flutter/foundation.dart';
import 'package:flutter/services.dart' show rootBundle;
import 'package:flutter_blue_plus/flutter_blue_plus.dart';
import 'package:http/http.dart' as http;
import 'package:shared_preferences/shared_preferences.dart';
import '../detection/detection_models.dart';

/// Owns the detection signature DB and which categories are enabled.
///
/// Load order: bundled asset (instant, offline) -> cached copy (last fetched)
/// -> remote fetch from the repo in the background (auto-update). Per-category
/// enable/disable is persisted; categories with "defaultOn: false" (e.g.
/// iBeacon) start disabled so they don't flood the list.
class DetectionProvider extends ChangeNotifier {
  static const _remoteUrl =
      'https://raw.githubusercontent.com/DaGromflomite/SleazyCrow/main/mobile_app/assets/detection/signatures.json';
  static const _assetPath = 'assets/detection/signatures.json';
  static const _prefsCache = 'detection_signatures_cache';
  static const _prefsDisabled = 'detection_disabled_categories';
  static const _prefsConfigured = 'detection_toggles_configured';

  SignatureDb? _db;
  final Set<String> _disabled = {};
  bool _loaded = false;
  String? _lastError;

  SignatureDb? get db => _db;
  bool get loaded => _loaded;
  String? get lastError => _lastError;
  List<DetectionCategory> get categories => _db?.categories ?? const [];

  bool isEnabled(String id) => !_disabled.contains(id);

  Future<void> init() async {
    // 1. bundled asset — always available
    try {
      final s = await rootBundle.loadString(_assetPath);
      _db = SignatureDb.fromJson(jsonDecode(s) as Map<String, dynamic>);
    } catch (e) {
      _lastError = 'bundled signatures failed: $e';
    }

    final prefs = await SharedPreferences.getInstance();

    // 2. cached (previous remote fetch) if newer than the bundled copy
    final cached = prefs.getString(_prefsCache);
    if (cached != null) {
      try {
        final c = SignatureDb.fromJson(jsonDecode(cached) as Map<String, dynamic>);
        if (_db == null || c.version > _db!.version) _db = c;
      } catch (_) {/* ignore bad cache */}
    }

    // Restore toggles. First run: seed disabled from each category's defaultOn.
    if (prefs.getBool(_prefsConfigured) == true) {
      _disabled
        ..clear()
        ..addAll(prefs.getStringList(_prefsDisabled) ?? const []);
    } else {
      _disabled
        ..clear()
        ..addAll(_db?.categories.where((c) => !c.defaultOn).map((c) => c.id) ??
            const ['ibeacon']);
    }

    _loaded = true;
    notifyListeners();

    // 3. background remote refresh (non-blocking)
    _refreshRemote();
  }

  Future<void> _refreshRemote() async {
    try {
      final resp = await http
          .get(Uri.parse(_remoteUrl))
          .timeout(const Duration(seconds: 6));
      if (resp.statusCode != 200) return;
      final remote = SignatureDb.fromJson(
          jsonDecode(resp.body) as Map<String, dynamic>);
      if (_db == null || remote.version >= _db!.version) {
        _db = remote;
        final prefs = await SharedPreferences.getInstance();
        await prefs.setString(_prefsCache, resp.body);
        notifyListeners();
      }
    } catch (_) {
      // offline / fetch failed — keep bundled or cached, no user-facing error
    }
  }

  Future<void> setEnabled(String id, bool on) async {
    if (on) {
      _disabled.remove(id);
    } else {
      _disabled.add(id);
    }
    notifyListeners();
    final prefs = await SharedPreferences.getInstance();
    await prefs.setStringList(_prefsDisabled, _disabled.toList());
    await prefs.setBool(_prefsConfigured, true);
  }

  /// Strongest detection for a classic-Bluetooth device, which only exposes a
  /// name (no advert fields). Matches name rules — this is how the card-skimmer
  /// modules (HC-05/06 etc.) get flagged.
  Detection? evaluateClassic(String? name) {
    final db = _db;
    if (db == null || name == null || name.isEmpty) return null;
    final lower = name.toLowerCase();
    Detection? best;
    for (final c in db.categories) {
      if (_disabled.contains(c.id)) continue;
      Grade? g;
      for (final rule in c.rules) {
        final t = rule.text;
        if (t == null) continue;
        final hit = (rule.type == 'nameContains' && lower.contains(t)) ||
            (rule.type == 'namePrefix' && lower.startsWith(t));
        if (hit && (g == null || rule.grade.index < g.index)) g = rule.grade;
      }
      if (g != null && (best == null || g.index < best.grade.index)) {
        best = Detection(c, g);
      }
    }
    return best;
  }

  /// Strongest detection for a BLE device among enabled categories, or null.
  Detection? evaluate(ScanResult r) {
    final db = _db;
    if (db == null) return null;
    Detection? best;
    for (final c in db.categories) {
      if (_disabled.contains(c.id)) continue;
      final g = c.evaluateBle(r);
      if (g != null && (best == null || g.index < best.grade.index)) {
        best = Detection(c, g);
      }
    }
    return best;
  }
}

import 'package:flutter_blue_plus/flutter_blue_plus.dart';

/// Detection signature model. Loaded from a hosted JSON (updatable without an
/// app release) with a bundled asset fallback. Reimplemented from public
/// sources — no GPL code or tables are copied.

enum Grade { high, med, low }

Grade _gradeFromString(String? s) {
  switch (s) {
    case 'high':
      return Grade.high;
    case 'low':
      return Grade.low;
    default:
      return Grade.med;
  }
}

extension GradeLabel on Grade {
  String get label => switch (this) {
        Grade.high => 'High',
        Grade.med => 'Medium',
        Grade.low => 'Low',
      };
}

/// One match rule. BLE rule types evaluate now; WiFi rule types are carried in
/// the DB but only match once the WiFi scanner (P4) is wired in.
class DetectionRule {
  final String type; // manufacturer | service | nameContains | namePrefix | wifiOui | wifiSsid
  final int? company; // manufacturer company id
  final List<int>? dataPrefix; // optional leading bytes of manufacturer data
  final String? uuid; // service UUID (lowercase, short or full)
  final String? text; // name / ssid text (lowercase)
  final Grade grade;

  DetectionRule({
    required this.type,
    this.company,
    this.dataPrefix,
    this.uuid,
    this.text,
    required this.grade,
  });

  static List<int>? _hexToBytes(String? hex) {
    if (hex == null || hex.isEmpty) return null;
    final clean = hex.replaceAll(' ', '');
    final out = <int>[];
    for (var i = 0; i + 1 < clean.length; i += 2) {
      final b = int.tryParse(clean.substring(i, i + 2), radix: 16);
      if (b == null) return null;
      out.add(b);
    }
    return out.isEmpty ? null : out;
  }

  factory DetectionRule.fromJson(Map<String, dynamic> j) {
    return DetectionRule(
      type: j['type'] as String,
      company: j['company'] as int?,
      dataPrefix: _hexToBytes(j['dataPrefix'] as String?),
      uuid: (j['uuid'] as String?)?.toLowerCase(),
      text: (j['text'] as String?)?.toLowerCase(),
      grade: _gradeFromString(j['grade'] as String?),
    );
  }

  /// True if this rule matches a BLE advertisement. WiFi rule types always
  /// return false here (handled later by the WiFi scanner).
  bool matchesBle(ScanResult r) {
    final adv = r.advertisementData;
    switch (type) {
      case 'manufacturer':
        if (company == null) return false;
        final data = adv.manufacturerData[company!];
        if (data == null) return false;
        final p = dataPrefix;
        if (p == null) return true;
        if (data.length < p.length) return false;
        for (var i = 0; i < p.length; i++) {
          if (data[i] != p[i]) return false;
        }
        return true;
      case 'service':
        if (uuid == null) return false;
        final want = fullUuid(uuid!);
        for (final g in adv.serviceUuids) {
          if (fullUuid(g.str128) == want) return true;
        }
        for (final g in adv.serviceData.keys) {
          if (fullUuid(g.str128) == want) return true;
        }
        return false;
      case 'nameContains':
        if (text == null) return false;
        return _name(r).contains(text!);
      case 'namePrefix':
        if (text == null) return false;
        return _name(r).startsWith(text!);
      default:
        return false; // wifiOui / wifiSsid: not a BLE match
    }
  }

  /// True if this rule matches a scanned WiFi access point. BLE rule types
  /// always return false here. [bssid] is the AP's MAC (any separator), [ssid]
  /// its network name.
  bool matchesWifi(String ssid, String bssid) {
    switch (type) {
      case 'wifiOui':
        if (text == null) return false;
        final oui = text!.replaceAll('-', ':');
        final b = bssid.toLowerCase().replaceAll('-', ':');
        return b.startsWith(oui);
      case 'wifiSsid':
        if (text == null) return false;
        return ssid.toLowerCase().contains(text!);
      default:
        return false; // BLE rule types: not a WiFi match
    }
  }

  /// A UUID in full 128-bit form, lowercase. 16- and 32-bit short UUIDs are
  /// expanded with the Bluetooth base UUID, so "feed" only matches 0xFEED and
  /// not any random 128-bit UUID that happens to contain "feed".
  static String fullUuid(String u) {
    final s = u.toLowerCase().trim();
    if (s.length == 4) return '0000$s-0000-1000-8000-00805f9b34fb';
    if (s.length == 8) return '$s-0000-1000-8000-00805f9b34fb';
    return s;
  }

  static String _name(ScanResult r) =>
      '${r.device.platformName} ${r.advertisementData.advName}'.toLowerCase();
}

class DetectionCategory {
  final String id;
  final String label;
  final String severity; // tracker | surveillance | drone | skimmer | tool | info
  final bool defaultOn;
  final List<DetectionRule> rules;

  DetectionCategory({
    required this.id,
    required this.label,
    required this.severity,
    required this.defaultOn,
    required this.rules,
  });

  factory DetectionCategory.fromJson(Map<String, dynamic> j) {
    return DetectionCategory(
      id: j['id'] as String,
      label: j['label'] as String,
      severity: (j['severity'] as String?) ?? 'info',
      defaultOn: (j['defaultOn'] as bool?) ?? true,
      rules: ((j['rules'] as List?) ?? [])
          .map((e) => DetectionRule.fromJson(e as Map<String, dynamic>))
          .toList(),
    );
  }

  /// Best (strongest) grade this category matches for a BLE advert, or null.
  Grade? evaluateBle(ScanResult r) {
    Grade? best;
    for (final rule in rules) {
      if (rule.matchesBle(r)) {
        if (best == null || rule.grade.index < best.index) best = rule.grade;
      }
    }
    return best;
  }

  /// Best (strongest) grade this category matches for a WiFi AP, or null.
  Grade? evaluateWifi(String ssid, String bssid) {
    Grade? best;
    for (final rule in rules) {
      if (rule.matchesWifi(ssid, bssid)) {
        if (best == null || rule.grade.index < best.index) best = rule.grade;
      }
    }
    return best;
  }
}

class SignatureDb {
  final int version;
  final String updated;
  final List<DetectionCategory> categories;

  SignatureDb({
    required this.version,
    required this.updated,
    required this.categories,
  });

  factory SignatureDb.fromJson(Map<String, dynamic> j) {
    return SignatureDb(
      version: (j['version'] as int?) ?? 0,
      updated: (j['updated'] as String?) ?? '',
      categories: ((j['categories'] as List?) ?? [])
          .map((e) => DetectionCategory.fromJson(e as Map<String, dynamic>))
          .toList(),
    );
  }
}

/// A category matched against a specific device, with the grade that fired.
class Detection {
  final DetectionCategory category;
  final Grade grade;
  Detection(this.category, this.grade);
}

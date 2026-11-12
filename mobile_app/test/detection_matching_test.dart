import 'dart:convert';
import 'dart:io';

import 'package:flutter_test/flutter_test.dart';
import 'package:sleazycrow/detection/detection_models.dart';

/// End-to-end matching tests for the detection engine's WiFi path against the
/// REAL shipped signatures.json. (flock + alpr were removed 2026-11-11: a
/// deployed Flock/ALPR runs on LTE and never broadcasts a WiFi AP a phone can
/// see, so keeping them was false advertising.) BLE matching needs a platform
/// ScanResult and is covered by on-device testing.
void main() {
  late SignatureDb db;
  setUpAll(() {
    db = SignatureDb.fromJson(jsonDecode(
        File('assets/detection/signatures.json').readAsStringSync()) as Map<String, dynamic>);
  });

  DetectionCategory cat(String id) => db.categories.firstWhere((c) => c.id == id);

  /// Strongest category id that fires for a WiFi AP (mirrors provider logic).
  String? firstHit(String ssid, String bssid) {
    for (final c in db.categories) {
      if (c.evaluateWifi(ssid, bssid) != null) return c.id;
    }
    return null;
  }

  test('flock + alpr are removed (phone cannot detect deployed LTE cameras)', () {
    expect(db.categories.any((c) => c.id == 'flock'), isFalse);
    expect(db.categories.any((c) => c.id == 'alpr'), isFalse);
  });

  group('Axon / Taser body camera', () {
    test('WiFi SSID "AB4-00123" matches high', () {
      expect(cat('axon').evaluateWifi('AB4-00123', 'aa:bb:cc:dd:ee:ff'), Grade.high);
    });
    test('WiFi SSID "Axon-body3" matches high', () {
      expect(cat('axon').evaluateWifi('Axon-body3', '11:22:33:44:55:66'), Grade.high);
    });
    test('WiFi OUI 00:25:df matches high', () {
      expect(cat('axon').evaluateWifi('any', '00:25:DF:12:34:56'), Grade.high);
    });
    test('has BLE manufacturer 845 and service fc81 for on-device BLE path', () {
      final r = cat('axon').rules;
      expect(r.any((x) => x.type == 'manufacturer' && x.company == 845), isTrue);
      expect(r.any((x) => x.type == 'service' && x.uuid == 'fc81'), isTrue);
    });
  });

  group('other WiFi-path surveillance categories fire on their OUIs', () {
    test('bodycam 00:23:bd', () => expect(cat('bodycam').evaluateWifi('x', '00:23:bd:00:00:01'), isNotNull));
    test('ring ac:9f:c3', () => expect(cat('ring').evaluateWifi('x', 'ac:9f:c3:00:00:01'), isNotNull));
    test('shotspotter d4:11:d6', () => expect(cat('shotspotter').evaluateWifi('x', 'd4:11:d6:00:00:01'), isNotNull));
  });

  group('no false positives on benign APs', () {
    test('home router does not match any category', () {
      expect(firstHit('MyHomeWiFi', 'a0:63:91:12:34:56'), isNull);
    });
    test('coffee shop AP does not match', () {
      expect(firstHit('Starbucks WiFi', 'de:ad:be:ef:00:01'), isNull);
    });
    test('empty ssid with unrelated OUI does not match', () {
      expect(firstHit('', '12:34:56:78:9a:bc'), isNull);
    });
  });

  test('strongest grade wins when multiple rules match (Axon OUI+SSID)', () {
    // OUI 00:25:df (high) + SSID "axon-" (high) both present -> high.
    expect(cat('axon').evaluateWifi('axon-7', '00:25:df:00:00:01'), Grade.high);
  });

  test('BLE rule types never match on the WiFi path', () {
    for (final c in db.categories) {
      for (final r in c.rules) {
        if (r.type == 'manufacturer' || r.type == 'service' ||
            r.type == 'nameContains' || r.type == 'namePrefix') {
          expect(r.matchesWifi('anything', 'aa:bb:cc:dd:ee:ff'), isFalse,
              reason: '${c.id} ${r.type} should not match WiFi');
        }
      }
    }
  });
}

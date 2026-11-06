import 'dart:convert';
import 'dart:io';

import 'package:flutter_test/flutter_test.dart';
import 'package:sleazycrow/detection/detection_models.dart';

/// Regression tests for the bundled detection signature DB and its parser.
/// These are pure-Dart (no BLE/device) so they run under `flutter test`.
void main() {
  late SignatureDb db;
  late Map<String, dynamic> raw;

  setUpAll(() {
    final file = File('assets/detection/signatures.json');
    expect(file.existsSync(), isTrue,
        reason: 'bundled signatures.json must exist');
    raw = jsonDecode(file.readAsStringSync()) as Map<String, dynamic>;
    db = SignatureDb.fromJson(raw);
  });

  test('DB parses with a sane version and category count', () {
    expect(db.version, greaterThanOrEqualTo(7));
    expect(db.categories.length, 19);
  });

  test('category ids are unique and non-empty', () {
    final ids = db.categories.map((c) => c.id).toList();
    expect(ids.toSet().length, ids.length, reason: 'duplicate category id');
    expect(ids.any((id) => id.trim().isEmpty), isFalse);
  });

  test('Meshtastic / LoRa category is removed, drone kept', () {
    final ids = db.categories.map((c) => c.id).toSet();
    expect(ids.contains('mesh'), isFalse,
        reason: 'LoRa/Meshtastic detection was dropped from scope');
    expect(ids.contains('drone'), isTrue);
  });

  test('iBeacon ships disabled by default; nothing else unexpectedly off', () {
    final off = db.categories.where((c) => !c.defaultOn).map((c) => c.id).toSet();
    expect(off, {'ibeacon'});
  });

  test('every rule is well-formed for its type', () {
    const known = {
      'manufacturer',
      'service',
      'nameContains',
      'namePrefix',
      'wifiOui',
      'wifiSsid',
    };
    for (final c in db.categories) {
      expect(c.rules, isNotEmpty, reason: '${c.id} has no rules');
      for (final r in c.rules) {
        expect(known.contains(r.type), isTrue,
            reason: '${c.id}: unknown rule type ${r.type}');
        switch (r.type) {
          case 'manufacturer':
            expect(r.company, isNotNull,
                reason: '${c.id}: manufacturer rule needs a company id');
            break;
          case 'service':
            expect(r.uuid, isNotNull,
                reason: '${c.id}: service rule needs a uuid');
            break;
          case 'nameContains':
          case 'namePrefix':
          case 'wifiOui':
          case 'wifiSsid':
            expect(r.text != null && r.text!.isNotEmpty, isTrue,
                reason: '${c.id}: ${r.type} rule needs text');
            break;
        }
      }
    }
  });

  test('text rules are stored lowercase (engine lowercases inputs)', () {
    for (final c in db.categories) {
      for (final r in c.rules) {
        if (r.text != null) {
          expect(r.text, r.text!.toLowerCase(),
              reason: '${c.id}: "${r.text}" must be lowercase to match');
        }
        if (r.uuid != null) {
          expect(r.uuid, r.uuid!.toLowerCase(),
              reason: '${c.id}: uuid "${r.uuid}" must be lowercase');
        }
      }
    }
  });

  test('known BLE anchors survive: AirTag, skimmer, drone, ShotSpotter', () {
    Grade? gradeFor(String id, bool Function(DetectionRule) pred) {
      final cat = db.categories.firstWhere((c) => c.id == id);
      for (final r in cat.rules) {
        if (pred(r)) return r.grade;
      }
      return null;
    }

    // Apple Find My (76) manufacturer data starting 0x12. Graded med, not high:
    // the 0x12 Find My advert is shared by AirTags AND findable Apple devices
    // (iPhones/AirPods/Macs), so a single hit is "likely", not certain.
    expect(
        gradeFor('airtag', (r) => r.type == 'manufacturer' && r.company == 76),
        Grade.med);
    // Card skimmer: HC-05 name.
    expect(gradeFor('skimmer', (r) => r.text == 'hc-05'), Grade.high);
    // Drone: OpenDroneID service 0xFFFA.
    expect(gradeFor('drone', (r) => r.uuid == 'fffa'), Grade.high);
    // ShotSpotter: IEEE-confirmed OUI d4:11:d6.
    expect(gradeFor('shotspotter', (r) => r.text == 'd4:11:d6'), Grade.high);
  });

  test('the "updated" field is present', () {
    expect((raw['updated'] as String?)?.isNotEmpty, isTrue);
  });
}

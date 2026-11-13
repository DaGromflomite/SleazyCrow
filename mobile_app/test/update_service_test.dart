import 'dart:convert';
import 'dart:io';

import 'package:flutter_test/flutter_test.dart';
import 'package:sleazycrow/services/update_service.dart';

/// OTA / self-update source tests, run against REAL GitHub releases JSON
/// (test/fixtures, captured from api.github.com on 2026-11-12):
///  - sleazycrow_releases.json : DaGromflomite/SleazyCrow (our repo)
///  - senape_releases.json     : Senape3000/EvilCrowRF-V2 (upstream)
void main() {
  List<dynamic> load(String name) =>
      jsonDecode(File('test/fixtures/$name').readAsStringSync()) as List;

  group('update source is our repo', () {
    test('API points at DaGromflomite/SleazyCrow', () {
      expect(UpdateService.githubOwner, 'DaGromflomite');
      expect(UpdateService.githubRepo, 'SleazyCrow');
    });

    test('firmware: picks our OTA bin + md5 from the live release', () {
      final pick = UpdateService.pickLatest(
          load('sleazycrow_releases.json'), UpdateService.firmwarePattern)!;
      expect(pick.version, '1.2.0');
      expect(pick.asset['name'], 'sleazycrow-fw-v1.2.0-beta-OTA.bin');
      expect(pick.md5Asset?['name'], 'sleazycrow-fw-v1.2.0-beta-OTA.bin.md5');
      expect(pick.asset['browser_download_url'] as String,
          startsWith('https://github.com/DaGromflomite/SleazyCrow/releases/download/'));
    });

    test('app: picks our signed APK + md5 from the live release', () {
      final pick = UpdateService.pickLatest(
          load('sleazycrow_releases.json'), UpdateService.apkPattern)!;
      expect(pick.version, '1.2.0');
      expect(pick.asset['name'], 'SleazyCrow-v1.2.0-beta.apk');
      expect(pick.md5Asset?['name'], 'SleazyCrow-v1.2.0-beta.apk.md5');
      expect(pick.asset['browser_download_url'] as String,
          contains('/DaGromflomite/SleazyCrow/'));
    });

    test('full / TEST builds are never offered as OTA', () {
      final p = UpdateService.firmwarePattern;
      expect(p.hasMatch('sleazycrow-fw-v1.2.0-beta-full.bin'), isFalse);
      expect(p.hasMatch('sleazycrow-fw-v1.2.0-TEST-OTA.bin'), isFalse);
      expect(UpdateService.apkPattern.hasMatch('SleazyCrow-v1.2.0-TEST.apk'), isFalse);
    });
  });

  group('upstream (Senape) assets can never be picked', () {
    test('Senape firmware/APK names do not match our patterns', () {
      final senape = load('senape_releases.json');
      expect(senape, isNotEmpty);
      expect(UpdateService.pickLatest(senape, UpdateService.firmwarePattern), isNull);
      expect(UpdateService.pickLatest(senape, UpdateService.apkPattern), isNull);
    });

    test('even mixed into our list, only our assets win', () {
      final mixed = [...load('senape_releases.json'), ...load('sleazycrow_releases.json')];
      final fw = UpdateService.pickLatest(mixed, UpdateService.firmwarePattern)!;
      final apk = UpdateService.pickLatest(mixed, UpdateService.apkPattern)!;
      expect(fw.asset['name'], startsWith('sleazycrow-fw-'));
      expect(apk.asset['name'], startsWith('SleazyCrow-v'));
    });
  });

  group('version gating', () {
    test('newer only, pre-release suffix ignored', () {
      expect(UpdateService.isNewer('1.2.0', '1.1.9'), isTrue);
      expect(UpdateService.isNewer('2.0.0', '1.2.5-dev'), isTrue);
      expect(UpdateService.isNewer('1.2.0', '1.2.0-beta'), isFalse);
      // current dev builds are ahead of the live 1.2.0-beta: no "update" offered
      expect(UpdateService.isNewer('1.2.0', '1.2.5-dev'), isFalse); // firmware
      expect(UpdateService.isNewer('1.2.0', '2.0.0-dev'), isFalse); // app
    });

    test('drafts are skipped', () {
      final rel = load('sleazycrow_releases.json')
          .map((r) => {...(r as Map<String, dynamic>), 'draft': true})
          .toList();
      expect(UpdateService.pickLatest(rel, UpdateService.apkPattern), isNull);
    });
  });

  group('unstable channel (opt-in)', () {
    Map<String, dynamic> rel(String tag, List<String> names, {bool pre = true}) => {
          'tag_name': tag, 'draft': false, 'prerelease': pre,
          'assets': [
            for (final n in names)
              {'name': n, 'browser_download_url':
                  'https://github.com/DaGromflomite/SleazyCrow/releases/download/$tag/$n'}
          ],
        };
    final releases = [
      rel('v2.0.0-unstable', [
        'sleazycrow-fw-v2.0.0-unstable-OTA.bin', 'sleazycrow-fw-v2.0.0-unstable-OTA.bin.md5',
        'SleazyCrow-v2.0.0-unstable.apk', 'SleazyCrow-v2.0.0-unstable.apk.md5',
      ]),
      ...load('sleazycrow_releases.json'),
    ];

    test('hidden unless the user opted in', () {
      expect(UpdateService.pickLatest(releases, UpdateService.firmwarePattern)!.version, '1.2.0');
      expect(UpdateService.pickLatest(releases, UpdateService.apkPattern)!.version, '1.2.0');
    });

    test('offered and flagged unstable when opted in', () {
      final fw = UpdateService.pickLatest(releases, UpdateService.firmwarePattern,
          includeUnstable: true)!;
      final apk = UpdateService.pickLatest(releases, UpdateService.apkPattern,
          includeUnstable: true)!;
      expect(fw.version, '2.0.0');
      expect(fw.unstable, isTrue);
      expect(fw.md5Asset?['name'], 'sleazycrow-fw-v2.0.0-unstable-OTA.bin.md5');
      expect(apk.version, '2.0.0');
      expect(apk.unstable, isTrue);
    });

    test('stable picks are never flagged unstable', () {
      final fw = UpdateService.pickLatest(load('sleazycrow_releases.json'),
          UpdateService.firmwarePattern, includeUnstable: true)!;
      expect(fw.unstable, isFalse);
    });

    test('the old v1.2.0-beta app cannot see unstable builds', () {
      // Exact patterns shipped in v1.2.0-beta (lib/services/update_service.dart).
      final oldFw = RegExp(r'sleazycrow-fw-v(\d+\.\d+\.\d+)(?:-beta)?-OTA\.bin$',
          caseSensitive: false);
      final oldApk = RegExp(r'SleazyCrow-v(\d+\.\d+\.\d+)(?:-beta)?\.apk$',
          caseSensitive: false);
      expect(oldFw.hasMatch('sleazycrow-fw-v2.0.0-unstable-OTA.bin'), isFalse);
      expect(oldApk.hasMatch('SleazyCrow-v2.0.0-unstable.apk'), isFalse);
    });
  });
}

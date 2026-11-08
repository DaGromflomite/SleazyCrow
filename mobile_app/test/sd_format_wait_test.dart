import 'package:flutter_test/flutter_test.dart';
import 'package:sleazycrow/providers/ble_provider.dart';

// The "format SD card" paths must wait for the Crow to say it's finished,
// not guess with a timer. These feed the provider the same messages the
// firmware sends (shape from BinaryMessageParser) and check the wait.
Map<String, dynamic> msg({required bool success, bool progress = false, String? text}) => {
      'data': {
        'action': 'format-sd',
        'success': success,
        'isProgress': progress,
        'progressMessage': text,
      },
    };

void main() {
  test('wait stays open through progress and ends on the final result', () async {
    final ble = BleProvider.forTesting();
    ble.debugBeginFormat();
    bool? result;
    var finished = false;
    ble.waitForSdFormat().then((r) {
      result = r;
      finished = true;
    });

    ble.debugFileSystemResponse(msg(success: true, progress: true, text: 'Deleting: /SUB Files'));
    await Future<void>.delayed(Duration.zero);
    expect(finished, isFalse, reason: 'a progress message is not the end of the format');
    expect(ble.isFormattingSD, isTrue);
    expect(ble.sdFormatProgress, 'Deleting: /SUB Files');

    ble.debugFileSystemResponse(msg(success: true));
    await Future<void>.delayed(Duration.zero);
    expect(finished, isTrue);
    expect(result, isTrue);
    expect(ble.isFormattingSD, isFalse);
  });

  test('a failed format reports false', () async {
    final ble = BleProvider.forTesting();
    ble.debugBeginFormat();
    final wait = ble.waitForSdFormat();
    ble.debugFileSystemResponse(msg(success: false));
    expect(await wait, isFalse);
  });

  test('a dropped link ends the wait with false instead of hanging', () async {
    final ble = BleProvider.forTesting();
    ble.debugBeginFormat();
    final wait = ble.waitForSdFormat();
    ble.debugLinkLost();
    expect(await wait, isFalse);
    expect(ble.isFormattingSD, isFalse);
  });

  test('no answer at all times out with null (not a fake success)', () async {
    final ble = BleProvider.forTesting();
    ble.debugBeginFormat();
    expect(await ble.waitForSdFormat(timeout: const Duration(milliseconds: 50)), isNull);
  });

  test('a long format is not cut off by a short timer', () async {
    final ble = BleProvider.forTesting();
    ble.debugBeginFormat();
    final wait = ble.waitForSdFormat(timeout: const Duration(seconds: 5));
    await Future<void>.delayed(const Duration(milliseconds: 300));
    ble.debugFileSystemResponse(msg(success: true));
    expect(await wait, isTrue);
  });

  test('the default timeout allows a full card several minutes', () {
    expect(BleProvider.sdFormatTimeout, greaterThanOrEqualTo(const Duration(minutes: 10)));
  });
}

import 'dart:typed_data';
import 'package:flutter_test/flutter_test.dart';
import 'package:sleazycrow/services/binary_message_parser.dart';

/// Verifies the app decodes the firmware's TPMS BLE messages exactly as the
/// C structs (BinaryTpmsHit / BinaryTpmsStatus) encode them. This catches any
/// byte-offset drift between firmware and app.
void main() {
  group('TPMS hit parsing', () {
    test('decodes sensor id, pressure, temp, rssi, protocol, crc', () {
      // [0xD8][sensorId:4LE][pressure_kpa:2LE][temp_c:i8][rssi:i8][protocol][flags]
      final b = Uint8List.fromList([
        0xD8,
        0x78, 0x56, 0x34, 0x12, // sensorId = 0x12345678 LE
        0xDC, 0x00, // pressure 220 kPa
        20, // temp +20C
        256 - 62, // rssi -62 (int8)
        1, // protocol = Schrader
        0x01, // crc ok
      ]);
      final r = BinaryMessageParser.parseBinaryMessage(b)!;
      expect(r['type'], 'TpmsHit');
      final d = r['data'] as Map;
      expect(d['sensorId'], 0x12345678);
      expect(d['pressureKpa'], 220);
      expect(d['temperatureC'], 20);
      expect(d['rssi'], -62);
      expect(d['protocol'], 1);
      expect(d['crcOk'], true);
    });

    test('unknown pressure (0xFFFF) and temp (0x80) decode to null', () {
      final b = Uint8List.fromList([
        0xD8,
        0x01, 0x00, 0x00, 0x00, // id = 1
        0xFF, 0xFF, // pressure unknown
        0x80, // temp unknown (-128)
        256 - 50, // rssi -50
        0, // generic protocol
        0x00, // no crc
      ]);
      final d = BinaryMessageParser.parseBinaryMessage(b)!['data'] as Map;
      expect(d['pressureKpa'], isNull);
      expect(d['temperatureC'], isNull);
      expect(d['crcOk'], false);
    });

    test('negative-looking high id stays a positive 32-bit value', () {
      final b = Uint8List.fromList([
        0xD8,
        0xFF, 0xFF, 0xFF, 0xEE, // 0xEEFFFFFF
        0xFF, 0xFF, 0x80, 256 - 40, 0, 0,
      ]);
      final d = BinaryMessageParser.parseBinaryMessage(b)!['data'] as Map;
      expect(d['sensorId'], 0xEEFFFFFF);
      expect(d['sensorId'] > 0, true);
    });
  });

  group('TPMS status parsing', () {
    test('decodes active, module, freq, seen, packets', () {
      // [0xD9][active][module][freqx100:2LE][seen:2LE][packets:2LE][reserved]
      final b = Uint8List.fromList([
        0xD9,
        1, // active
        0, // module 0
        0x0C, 0x7B, // 31500 = 315.00 MHz
        0x03, 0x00, // seen 3
        0x2A, 0x00, // packets 42
        0,
      ]);
      final r = BinaryMessageParser.parseBinaryMessage(b)!;
      expect(r['type'], 'TpmsStatus');
      final d = r['data'] as Map;
      expect(d['active'], true);
      expect(d['module'], 0);
      expect(d['freqMHz'], 315.0);
      expect(d['seen'], 3);
      expect(d['packets'], 42);
    });

    test('433.92 MHz EU frequency decodes', () {
      final b = Uint8List.fromList(
          [0xD9, 1, 1, 0x80, 0xA9, 0, 0, 0, 0, 0]); // 43392 = 433.92
      final d = BinaryMessageParser.parseBinaryMessage(b)!['data'] as Map;
      expect(d['freqMHz'], 433.92);
      expect(d['module'], 1);
    });
  });
}

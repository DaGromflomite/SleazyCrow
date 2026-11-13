import 'dart:io';
import 'dart:typed_data';

import 'package:flutter_test/flutter_test.dart';
import 'package:sleazycrow/providers/firmware_protocol.dart';
import 'package:sleazycrow/services/file_parsers/file_parser_factory.dart';
import 'package:sleazycrow/services/flipper_subdb_service.dart';

// The Crow's upload chunk counters are one byte. These tests pin the limit
// and check that big RAW .sub files are split into pieces the Crow can take
// whole, without losing a single sample.

int chunksFor(int bytes) =>
    1 + ((bytes + FirmwareBinaryProtocol.UPLOAD_CHUNK_DATA - 1) ~/
        FirmwareBinaryProtocol.UPLOAD_CHUNK_DATA);

List<String> rawLines(String text) => text
    .split('\n')
    .map((l) => l.trimRight())
    .where((l) => l.startsWith('RAW_Data:'))
    .toList();

String header(String text) => text
    .split('\n')
    .map((l) => l.trimRight())
    .takeWhile((l) => !l.startsWith('RAW_Data:'))
    .join('\n');

void checkSplit(String name, Uint8List bytes) {
  final entry = SubFileEntry(relativePath: 'Gates/$name', content: bytes);
  final pieces = FlipperSubDbService.splitIfLargeRaw(entry);
  final original = String.fromCharCodes(bytes);

  if (bytes.length <= FirmwareBinaryProtocol.MAX_UPLOAD_BYTES) {
    expect(pieces, hasLength(1), reason: '$name fits, must not be split');
    return;
  }
  expect(pieces.length, greaterThan(1), reason: '$name is over the limit');
  final joined = <String>[];
  for (final p in pieces) {
    expect(p.content.length, lessThanOrEqualTo(FirmwareBinaryProtocol.MAX_UPLOAD_BYTES),
        reason: '${p.relativePath} is still too big to upload whole');
    expect(chunksFor(p.content.length), lessThanOrEqualTo(255));
    final text = String.fromCharCodes(p.content);
    expect(header(text), header(original), reason: 'every piece keeps the full header');
    expect(p.relativePath, matches(RegExp(r'^Gates/.+_\d\d\.sub$')));
    final parsed = FileParserFactory.parseFile(text, filename: p.relativePath);
    expect(parsed.success, isTrue,
        reason: '${p.relativePath} is not a valid .sub for the app: ${parsed.errors}');
    joined.addAll(rawLines(text));
  }
  expect(joined, rawLines(original), reason: 'samples lost or reordered when splitting $name');
}

void main() {
  test('upload limit is exactly what one-byte chunk counters allow', () {
    expect(chunksFor(FirmwareBinaryProtocol.MAX_UPLOAD_BYTES), 255);
    expect(chunksFor(FirmwareBinaryProtocol.MAX_UPLOAD_BYTES + 1), 256,
        reason: 'one byte more would wrap the counter');
    expect(FirmwareBinaryProtocol.MAX_UPLOAD_BYTES, 124968);
  });

  test('a split is triggered by anything over the limit, pieces fit under it', () {
    expect(FlipperSubDbService.splitThresholdBytes, FirmwareBinaryProtocol.MAX_UPLOAD_BYTES);
    expect(FlipperSubDbService.splitPieceTargetBytes,
        lessThan(FirmwareBinaryProtocol.MAX_UPLOAD_BYTES - 4096));
  });

  test('synthetic CRLF RAW file just over the limit splits cleanly', () {
    final b = StringBuffer('Filetype: Flipper SubGhz RAW File\r\nVersion: 1\r\n'
        'Frequency: 433920000\r\nPreset: FuriHalSubGhzPresetOok650Async\r\nProtocol: RAW\r\n');
    var i = 0;
    while (b.length < FirmwareBinaryProtocol.MAX_UPLOAD_BYTES + 5000) {
      b.write('RAW_Data: ${List.generate(60, (k) => k.isEven ? 400 + i : -(800 + k)).join(' ')}\r\n');
      i++;
    }
    checkSplit('synthetic.sub', Uint8List.fromList(b.toString().codeUnits));
  });

  test('a small file is never split', () {
    final text = 'Filetype: Flipper SubGhz RAW File\nVersion: 1\nFrequency: 433920000\n'
        'Preset: FuriHalSubGhzPresetOok650Async\nProtocol: RAW\nRAW_Data: 500 -500 500 -500\n';
    checkSplit('small.sub', Uint8List.fromList(text.codeUnits));
  });

  // Real UberGuidoZ files, if downloaded into REAL_SUB_DIR (skipped otherwise).
  final dir = Platform.environment['REAL_SUB_DIR'];
  final files = dir == null
      ? <File>[]
      : Directory(dir).listSync().whereType<File>().where((f) => f.path.endsWith('.sub')).toList();
  for (final f in files) {
    test('real UberGuidoZ file splits losslessly: ${f.uri.pathSegments.last}', () {
      checkSplit(f.uri.pathSegments.last, f.readAsBytesSync());
    });
  }

  test('upload chunk header encodes the last allowed chunk without wrapping', () {
    final cmd = FirmwareBinaryProtocol.createUploadFileChunkCommand(
        Uint8List(10), 7, 255, 255);
    expect(cmd[3], 255);
    expect(cmd[4], 255);
  });
}

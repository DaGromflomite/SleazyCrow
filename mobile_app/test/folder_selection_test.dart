import 'dart:typed_data';

import 'package:flutter_test/flutter_test.dart';
import 'package:sleazycrow/services/flipper_subdb_service.dart';

// Covers the drill-down folder picker: browsing the Sub-GHz tree level by
// level, and the prefix-based selection that lets a user grab a whole top
// folder OR a specific deeper one (e.g. just TouchTunes/raw).

SubFileEntry stub(String path) =>
    SubFileEntry(relativePath: path, content: Uint8List(0));

final catalog = <SubFileEntry>[
  stub('TouchTunes/raw/Vup.sub'),
  stub('TouchTunes/raw/Down.sub'),
  stub('TouchTunes/pin/000/On.sub'),
  stub('TouchTunes/pin/000/Off.sub'),
  stub('TouchTunes/pin/001/On.sub'),
  stub('TouchTunes/brute/dump.sub'),
  stub('Garages/CAME/gate.sub'),
  stub('top_level.sub'),
];

const root = FlipperSubDbService.rootCategory;

void main() {
  group('folderOf', () {
    test('returns the containing folder path', () {
      expect(FlipperSubDbService.folderOf('TouchTunes/raw/Vup.sub'),
          'TouchTunes/raw');
      expect(FlipperSubDbService.folderOf('Garages/CAME/gate.sub'),
          'Garages/CAME');
    });
    test('root-level files report the root category', () {
      expect(FlipperSubDbService.folderOf('top_level.sub'), root);
    });
  });

  group('folderCovers', () {
    test('a folder covers itself and everything beneath it', () {
      expect(FlipperSubDbService.folderCovers('TouchTunes', 'TouchTunes/raw'),
          isTrue);
      expect(
          FlipperSubDbService.folderCovers('TouchTunes/raw', 'TouchTunes/raw'),
          isTrue);
    });
    test('a deeper pick does not reach into sibling folders', () {
      expect(
          FlipperSubDbService.folderCovers('TouchTunes/raw', 'TouchTunes/pin'),
          isFalse);
    });
    test('root category covers only root-level files', () {
      expect(FlipperSubDbService.folderCovers(root, root), isTrue);
      expect(FlipperSubDbService.folderCovers(root, 'TouchTunes'), isFalse);
      expect(FlipperSubDbService.folderCovers('TouchTunes', root), isFalse);
    });
    test('a name is not a prefix of a longer sibling name', () {
      // "Touch" must not accidentally cover "TouchTunes/...".
      expect(FlipperSubDbService.folderCovers('Touch', 'TouchTunes/raw'),
          isFalse);
    });
  });

  group('browseFolder', () {
    test('root lists top folders with recursive counts and root files', () {
      final b = FlipperSubDbService.browseFolder(catalog, '');
      expect(b.filesHere, 1); // top_level.sub
      final byName = {for (final n in b.folders) n.name: n};
      expect(byName.keys.toList(), ['Garages', 'TouchTunes']); // sorted
      expect(byName['TouchTunes']!.fileCount, 6); // raw2 + pin3 + brute1
      expect(byName['TouchTunes']!.hasChildren, isTrue);
      expect(byName['TouchTunes']!.path, 'TouchTunes');
      expect(byName['Garages']!.hasChildren, isTrue);
    });

    test('one level down shows immediate subfolders only', () {
      final b = FlipperSubDbService.browseFolder(catalog, 'TouchTunes');
      expect(b.filesHere, 0);
      final byName = {for (final n in b.folders) n.name: n};
      expect(byName.keys.toList(), ['brute', 'pin', 'raw']);
      expect(byName['raw']!.fileCount, 2);
      expect(byName['raw']!.hasChildren, isFalse);
      expect(byName['pin']!.fileCount, 3);
      expect(byName['pin']!.hasChildren, isTrue); // 000, 001
      expect(byName['pin']!.path, 'TouchTunes/pin');
    });

    test('a leaf folder reports its files directly, no subfolders', () {
      final b = FlipperSubDbService.browseFolder(catalog, 'TouchTunes/raw');
      expect(b.folders, isEmpty);
      expect(b.filesHere, 2);
    });

    test('nested pin codes are their own folders', () {
      final b = FlipperSubDbService.browseFolder(catalog, 'TouchTunes/pin');
      final byName = {for (final n in b.folders) n.name: n};
      expect(byName.keys.toList(), ['000', '001']);
      expect(byName['000']!.fileCount, 2);
      expect(byName['001']!.fileCount, 1);
    });
  });

  group('filterSelection (prefix-based)', () {
    List<String> paths(List<SubFileEntry> f) =>
        f.map((e) => e.relativePath).toList()..sort();

    test('picking only a deep folder grabs just that folder', () {
      final got = FlipperSubDbService.filterSelection(
        catalog,
        categories: {'TouchTunes/raw'},
        excludeBruteForce: true,
      );
      expect(paths(got), ['TouchTunes/raw/Down.sub', 'TouchTunes/raw/Vup.sub']);
    });

    test('picking a top folder grabs the whole subtree', () {
      final got = FlipperSubDbService.filterSelection(
        catalog,
        categories: {'TouchTunes'},
        excludeBruteForce: false,
      );
      expect(got.length, 6);
    });

    test('brute-force folders drop out when excluded', () {
      final got = FlipperSubDbService.filterSelection(
        catalog,
        categories: {'TouchTunes'},
        excludeBruteForce: true,
      );
      expect(got.length, 5);
      expect(got.any((e) => e.relativePath.contains('brute')), isFalse);
    });

    test('a single pin code resolves', () {
      final got = FlipperSubDbService.filterSelection(
        catalog,
        categories: {'TouchTunes/pin/000'},
        excludeBruteForce: true,
      );
      expect(paths(got),
          ['TouchTunes/pin/000/Off.sub', 'TouchTunes/pin/000/On.sub']);
    });

    test('root category selects only root-level files', () {
      final got = FlipperSubDbService.filterSelection(
        catalog,
        categories: {root},
        excludeBruteForce: true,
      );
      expect(paths(got), ['top_level.sub']);
    });

    test('overlapping picks never double-count a file', () {
      final got = FlipperSubDbService.filterSelection(
        catalog,
        categories: {'TouchTunes', 'TouchTunes/raw'},
        excludeBruteForce: false,
      );
      expect(got.length, 6); // not 8
    });

    test('old top-level-only saved selections still work', () {
      final got = FlipperSubDbService.filterSelection(
        catalog,
        categories: {'Garages'},
        excludeBruteForce: true,
      );
      expect(paths(got), ['Garages/CAME/gate.sub']);
    });
  });
}

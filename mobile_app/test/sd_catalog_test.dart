import 'package:flutter_test/flutter_test.dart';
import 'package:sleazycrow/services/sd_catalog.dart';

// The phone's offline list of the Crow's SD card (for building playlists
// without the Crow). These cover the path rules and how a fresh folder
// listing corrects what the phone remembered.
void main() {
  test('paths from every app path type end up SD-absolute', () {
    expect(SdCatalog.toSdPath('SUB Files/Gates/a.sub', 5), '/SUB Files/Gates/a.sub');
    expect(SdCatalog.toSdPath('/SUB Files//Gates/', 5), '/SUB Files/Gates');
    expect(SdCatalog.toSdPath('/', 5), '/');
    expect(SdCatalog.toSdPath('x.sub', 0), '/DATA/RECORDS/x.sub');
    expect(SdCatalog.toSdPath('/', 1), '/DATA/SIGNALS');
    expect(SdCatalog.toSdPath('/DATA/SIGNALS/y.sub', 1), '/DATA/SIGNALS/y.sub');
    expect(SdCatalog.toSdPath('z.sub', 4), '', reason: 'LittleFS is not on the SD card');
  });

  test('a folder listing replaces that folder and keeps everything else', () {
    final known = {
      '/SUB Files/Gates/old.sub', // deleted on the card since
      '/SUB Files/Gates/keep.sub',
      '/SUB Files/Gates/Brute/deep.sub', // subfolder still there
      '/SUB Files/Gates/Gone/x.sub', // subfolder removed since
      '/SUB Files/Doorbells/bell.sub', // other folder: untouched
    };
    final next = SdCatalog.applyListing(
        known, '/SUB Files/Gates', ['Brute'], ['keep.sub', 'new.sub', 'notes.txt']);
    expect(next, {
      '/SUB Files/Gates/keep.sub',
      '/SUB Files/Gates/new.sub',
      '/SUB Files/Gates/Brute/deep.sub',
      '/SUB Files/Doorbells/bell.sub',
    });
  });

  test('listing the card root works too', () {
    final next = SdCatalog.applyListing({'/a.sub', '/F/b.sub', '/G/c.sub'}, '/', ['F'], ['d.sub']);
    expect(next, {'/F/b.sub', '/d.sub'});
  });

  test('a similarly named folder is not mistaken for the listed one', () {
    final next = SdCatalog.applyListing({'/SUB Files/Gates2/a.sub'}, '/SUB Files/Gates', [], []);
    expect(next, {'/SUB Files/Gates2/a.sub'});
  });

  test('browsing: folders and files directly inside a folder', () {
    final all = {'/SUB Files/Gates/a.sub', '/SUB Files/Gates/Brute/b.sub', '/SUB Files/Doorbells/c.sub', '/top.sub'};
    final root = SdCatalog.children(all, '/');
    expect(root.dirs, ['SUB Files']);
    expect(root.files, ['/top.sub']);
    final gates = SdCatalog.children(all, '/SUB Files/Gates');
    expect(gates.dirs, ['Brute']);
    expect(gates.files, ['/SUB Files/Gates/a.sub']);
    expect(SdCatalog.parentOf('/SUB Files/Gates'), '/SUB Files');
    expect(SdCatalog.parentOf('/SUB Files'), '/');
  });
}

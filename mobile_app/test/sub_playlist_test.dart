import 'package:flutter_test/flutter_test.dart';
import 'package:sleazycrow/models/sub_playlist.dart';

/// Pure-Dart tests for the playlist model: file vs pause entries, JSON
/// round-trip, and the file/pause counting the UI relies on.
void main() {
  test('file item carries path + pathType, not a pause', () {
    final f = PlaylistItem.file(path: '/SUB Files/Garages/came.sub');
    expect(f.isFile, isTrue);
    expect(f.isPause, isFalse);
    expect(f.pathType, 5);
    expect(f.displayName, 'came.sub');
  });

  test('pause item renders a human duration and has no path', () {
    expect(PlaylistItem.pause(500).displayName, 'Pause 0.5s');
    expect(PlaylistItem.pause(2000).displayName, 'Pause 2s');
    expect(PlaylistItem.pause(2000).path, '');
    expect(PlaylistItem.pause(2000).isPause, isTrue);
  });

  test('JSON round-trip preserves a mixed file/pause playlist', () {
    final pl = SubPlaylist(
      name: 'Gate sweep',
      items: [
        PlaylistItem.file(path: '/SUB Files/a.sub'),
        PlaylistItem.pause(1000),
        PlaylistItem.file(path: '/SUB Files/b.sub', pathType: 5),
      ],
      gapMs: 400,
      repeatEach: 3,
      loop: true,
    );

    final restored = SubPlaylist.fromJson(pl.toJson());
    expect(restored.name, 'Gate sweep');
    expect(restored.items.length, 3);
    expect(restored.items[0].isFile, isTrue);
    expect(restored.items[0].path, '/SUB Files/a.sub');
    expect(restored.items[1].isPause, isTrue);
    expect(restored.items[1].pauseMs, 1000);
    expect(restored.items[2].path, '/SUB Files/b.sub');
    expect(restored.gapMs, 400);
    expect(restored.repeatEach, 3);
    expect(restored.loop, isTrue);
  });

  test('fileCount counts only files, not pauses', () {
    final pl = SubPlaylist(name: 'x', items: [
      PlaylistItem.file(path: '/a.sub'),
      PlaylistItem.pause(500),
      PlaylistItem.file(path: '/b.sub'),
      PlaylistItem.pause(500),
    ]);
    expect(pl.fileCount, 2);
    expect(pl.items.length, 4);
  });

  test('defaults are sane for a fresh playlist', () {
    final pl = SubPlaylist(name: 'new');
    expect(pl.items, isEmpty);
    expect(pl.repeatEach, 1);
    expect(pl.loop, isFalse);
    expect(pl.gapMs, greaterThanOrEqualTo(0));
  });
}

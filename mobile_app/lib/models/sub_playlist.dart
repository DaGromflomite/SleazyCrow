library;

/// Models for Sub-GHz transmit playlists: an ordered list the Crow plays in
/// sequence. Each entry is either a saved .sub FILE to transmit or an explicit
/// PAUSE of a chosen length, so the user can space signals right in the
/// builder. Stored app-side (JSON in SharedPreferences) — no firmware change;
/// playback reuses the normal transmitFromFile path, paced on the firmware's
/// burst-complete (SignalSent) event.

/// One playlist entry: a file to transmit, or a pause.
class PlaylistItem {
  /// 'file' or 'pause'.
  final String kind;

  /// Absolute path of the .sub file on the Crow's storage (file entries),
  /// e.g. "/SUB Files/Garages/CAME_12bit.sub".
  final String path;

  /// Firmware pathType the file lives under. 5 = SD root (the browser default).
  final int pathType;

  /// Pause length in milliseconds (pause entries).
  final int pauseMs;

  const PlaylistItem._({
    required this.kind,
    required this.path,
    required this.pathType,
    required this.pauseMs,
  });

  factory PlaylistItem.file({required String path, int pathType = 5}) =>
      PlaylistItem._(kind: 'file', path: path, pathType: pathType, pauseMs: 0);

  factory PlaylistItem.pause(int ms) =>
      PlaylistItem._(kind: 'pause', path: '', pathType: 5, pauseMs: ms);

  bool get isPause => kind == 'pause';
  bool get isFile => kind == 'file';

  /// Label shown in the UI.
  String get displayName {
    if (isPause) {
      final s = pauseMs / 1000.0;
      return 'Pause ${s.toStringAsFixed(s.truncateToDouble() == s ? 0 : 1)}s';
    }
    final parts = path.split('/');
    final base = parts.isEmpty ? path : parts.last;
    return base.isEmpty ? path : base;
  }

  factory PlaylistItem.fromJson(Map<String, dynamic> j) => PlaylistItem._(
        kind: (j['kind'] as String?) ?? 'file',
        path: (j['path'] as String?) ?? '',
        pathType: (j['pathType'] as int?) ?? 5,
        pauseMs: (j['pauseMs'] as int?) ?? 0,
      );

  Map<String, dynamic> toJson() => {
        'kind': kind,
        if (isFile) 'path': path,
        if (isFile) 'pathType': pathType,
        if (isPause) 'pauseMs': pauseMs,
      };
}

class SubPlaylist {
  String name;
  List<PlaylistItem> items;

  /// Gap between entries, in milliseconds (on top of the firmware's own
  /// burst-complete gating, and separate from explicit Pause entries).
  int gapMs;

  /// How many times to transmit EACH file before moving on (pauses ignore it).
  int repeatEach;

  /// Loop the whole list until the user stops.
  bool loop;

  SubPlaylist({
    required this.name,
    List<PlaylistItem>? items,
    this.gapMs = 300,
    this.repeatEach = 1,
    this.loop = false,
  }) : items = items ?? [];

  int get fileCount => items.where((e) => e.isFile).length;

  factory SubPlaylist.fromJson(Map<String, dynamic> j) => SubPlaylist(
        name: (j['name'] as String?) ?? 'Playlist',
        items: ((j['items'] as List?) ?? [])
            .map((e) => PlaylistItem.fromJson(e as Map<String, dynamic>))
            .toList(),
        gapMs: (j['gapMs'] as int?) ?? 300,
        repeatEach: (j['repeatEach'] as int?) ?? 1,
        loop: (j['loop'] as bool?) ?? false,
      );

  Map<String, dynamic> toJson() => {
        'name': name,
        'items': items.map((e) => e.toJson()).toList(),
        'gapMs': gapMs,
        'repeatEach': repeatEach,
        'loop': loop,
      };
}

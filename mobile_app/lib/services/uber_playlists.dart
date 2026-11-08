import '../models/sub_playlist.dart';
import 'flipper_subdb_service.dart';

/// One of UberGuidoZ's ready-made Flipper Sub-GHz playlists (a .txt of
/// `sub: /ext/subghz/...` lines), plus the files it needs once parsed.
class UberPlaylist {
  /// Repo-relative path, e.g. "subplaylist/Fans/Fans_on.txt".
  final String repoPath;

  /// Raw text (empty until fetched).
  final String text;

  const UberPlaylist({required this.repoPath, this.text = ''});

  UberPlaylist withText(String t) => UberPlaylist(repoPath: repoPath, text: t);

  /// Name shown in the app, e.g. "Uber · Fans / Fans_on".
  String get displayName => UberPlaylists.nameFor(repoPath);
}

/// Result of matching a playlist against the Sub-GHz catalog.
class UberPlaylistMatch {
  /// Sub-GHz-relative paths (catalog spelling), in playlist order.
  final List<String> files;

  /// Entries the playlist names that aren't in UberGuidoZ's own repo.
  final List<String> missing;

  const UberPlaylistMatch(this.files, this.missing);
}

/// Pure helpers for importing UberGuidoZ's playlists (unit-tested).
class UberPlaylists {
  UberPlaylists._();

  /// Prefix every app-made Uber playlist gets, so re-imports replace them.
  static const String namePrefix = 'Uber · ';

  /// Is [repoPath] (relative to the repo root) one of Uber's Sub-GHz playlists?
  /// They live in `subplaylist/`, plus the odd `*playlist*.txt` inside Sub-GHz.
  static bool isPlaylistPath(String repoPath) {
    final l = repoPath.toLowerCase();
    if (!l.endsWith('.txt')) return false;
    if (l.startsWith('subplaylist/')) return true;
    if (l.startsWith('sub-ghz/')) {
      final base = l.substring(l.lastIndexOf('/') + 1);
      return base.contains('playlist');
    }
    return false;
  }

  static String nameFor(String repoPath) {
    var p = repoPath;
    if (p.toLowerCase().startsWith('subplaylist/')) {
      p = p.substring('subplaylist/'.length);
    } else {
      p = p.substring(p.lastIndexOf('/') + 1); // Sub-GHz ones: just the file
    }
    if (p.toLowerCase().endsWith('.txt')) p = p.substring(0, p.length - 4);
    return '$namePrefix${p.split('/').join(' / ')}';
  }

  /// The `sub:` entries of a Flipper playlist, as Sub-GHz-relative paths
  /// ("/ext/subghz/Fans/x.sub" -> "Fans/x.sub"). Other lines are ignored.
  static List<String> parse(String text) {
    final out = <String>[];
    for (var line in text.split('\n')) {
      line = line.trim();
      if (!line.toLowerCase().startsWith('sub:')) continue;
      var p = line.substring(4).trim().replaceAll('\\', '/');
      final i = p.toLowerCase().indexOf('/subghz/');
      if (i >= 0) {
        p = p.substring(i + '/subghz/'.length);
      } else if (p.startsWith('/')) {
        p = p.substring(1);
      }
      if (p.isNotEmpty) out.add(p);
    }
    return out;
  }

  /// Lookup key: case-insensitive and treating spaces like underscores (a few
  /// of Uber's lists say "Egg Vibrator" where the folder is "Egg_Vibrator").
  static String key(String relPath) => relPath.toLowerCase().replaceAll(' ', '_');

  static Map<String, String> indexCatalog(Iterable<String> relPaths) =>
      {for (final p in relPaths) key(p): p};

  /// Match a playlist's entries against the catalog index from [indexCatalog].
  static UberPlaylistMatch match(String text, Map<String, String> index) {
    final files = <String>[];
    final missing = <String>[];
    for (final p in parse(text)) {
      final hit = index[key(p)];
      if (hit != null) {
        files.add(hit);
      } else {
        missing.add(p);
      }
    }
    return UberPlaylistMatch(files, missing);
  }

  /// Build the app playlist. [onCard] maps a Sub-GHz-relative path to the
  /// path(s) that actually landed on the card (a big RAW file becomes several
  /// pieces); files with no entry there are left out.
  static ({SubPlaylist playlist, int skipped}) build(
      UberPlaylist pl, UberPlaylistMatch m, Map<String, List<String>> onCard) {
    final items = <PlaylistItem>[];
    var skipped = m.missing.length;
    for (final f in m.files) {
      final landed = onCard[f];
      if (landed == null || landed.isEmpty) {
        skipped++;
        continue;
      }
      for (final r in landed) {
        items.add(PlaylistItem.file(
            path: '/${FlipperSubDbService.sdTargetFolder}/$r', pathType: 5));
      }
    }
    return (playlist: SubPlaylist(name: pl.displayName, items: items), skipped: skipped);
  }

  /// [existing] with each of [incoming] added, replacing any same-named one.
  static List<SubPlaylist> merge(List<SubPlaylist> existing, List<SubPlaylist> incoming) {
    final out = List<SubPlaylist>.of(existing);
    for (final p in incoming) {
      final i = out.indexWhere((e) => e.name == p.name);
      if (i >= 0) {
        out[i] = p;
      } else {
        out.add(p);
      }
    }
    return out;
  }
}

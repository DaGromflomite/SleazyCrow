import 'dart:convert';
import 'dart:io';
import 'dart:typed_data';
import 'package:http/http.dart' as http;
import 'package:path_provider/path_provider.dart';
import '../providers/firmware_protocol.dart';
import 'uber_playlists.dart';

/// Entry representing a .sub file from the source SubGHz collection.
///
/// During the *browse* phase an entry is a lightweight **stub** — its
/// [relativePath] is set and [content] is empty — so the selection UI can count
/// and filter by folder without downloading anything. Real [content] is fetched
/// only for the files the user actually picks (see [downloadEntries]).
class SubFileEntry {
  /// Path preserving subfolder structure, relative to the source's Sub-GHz
  /// root (e.g. "Garages/CAME/gate.sub"). This is saved verbatim under
  /// [sdTargetFolder] on the Crow's SD so the source's folder layout is
  /// reproduced exactly (which also keeps path-based playlists resolvable).
  final String relativePath;

  /// Raw file bytes. Empty for a catalog stub until downloaded.
  final Uint8List content;

  /// Byte size from the git tree (available for stubs; 0 if unknown).
  final int size;

  const SubFileEntry({
    required this.relativePath,
    required this.content,
    this.size = 0,
  });

  bool get isStub => content.isEmpty;
}

/// Downloads FlipperZero `.sub` files from **UberGuidoZ/Flipper** — but only the
/// `Sub-GHz/` tree (the Crow can't use his NFC / IR / BadUSB / etc.), and only
/// the folders the user selects.
///
/// Why browse-by-folder instead of a repo ZIP: `UberGuidoZ/Flipper` is a ~2 GB
/// monorepo and GitHub only zips a *whole* repo, so the old whole-ZIP approach
/// (used for the small Zero-Sploit DB) is impossible here. Instead we:
///   1. one Git Trees API call lists every `.sub` under `Sub-GHz/` (paths+sizes,
///      no content) — the "catalog";
///   2. the user picks top-level folders;
///   3. we fetch the picked files' bytes individually from raw.githubusercontent
///      (which doesn't count against the 60/hr API limit).
class FlipperSubDbService {
  // ---- Source repository (UberGuidoZ/Flipper, Sub-GHz subtree) -------------
  static const String _owner = 'UberGuidoZ';
  static const String _repo = 'Flipper';
  static const String _branch = 'main';
  static const String _subdir = 'Sub-GHz';

  static const String _treesApi =
      'https://api.github.com/repos/$_owner/$_repo/git/trees';
  static const String _rawBase =
      'https://raw.githubusercontent.com/$_owner/$_repo/$_branch/$_subdir/';

  /// GitHub rejects API requests with no User-Agent, so always send one.
  static const Map<String, String> _apiHeaders = {
    'User-Agent': 'SleazyCrow-App',
    'Accept': 'application/vnd.github+json',
  };

  /// Target folder name on the device SDCard.
  static const String sdTargetFolder = 'SUB Files';

  /// Label for files sitting directly at the Sub-GHz root (no category folder).
  static const String rootCategory = '(root)';

  /// Uber's ready-made playlists found by the last [fetchCatalog] (text not
  /// fetched yet; see [fetchPlaylistTexts]).
  static List<UberPlaylist> lastPlaylists = [];

  // ---------------------------------------------------------------------------
  // Catalog — list the Sub-GHz tree without downloading file contents.
  // ---------------------------------------------------------------------------

  /// Fetch the list of every `.sub` under the source's `Sub-GHz/` folder as
  /// lightweight stubs (path + size, no content). One Trees API call.
  ///
  /// [onProgress] phases: "catalog".
  static Future<List<SubFileEntry>> fetchCatalog({
    void Function(String phase, String detail, double fraction)? onProgress,
  }) async {
    onProgress?.call('catalog', 'Listing folders from UberGuidoZ…', 0.0);

    // 1. Resolve the Sub-GHz subtree SHA from the branch root tree.
    final rootResp = await http
        .get(Uri.parse('$_treesApi/$_branch'), headers: _apiHeaders)
        .timeout(const Duration(seconds: 20));
    if (rootResp.statusCode != 200) {
      throw Exception('GitHub tree list failed: HTTP ${rootResp.statusCode}');
    }
    final rootJson = jsonDecode(rootResp.body) as Map<String, dynamic>;
    final rootTree = (rootJson['tree'] as List?) ?? const [];
    String? subSha;
    String? playlistSha;
    for (final t in rootTree) {
      final m = t as Map<String, dynamic>;
      if (m['type'] != 'tree') continue;
      if (m['path'] == _subdir) subSha = m['sha'] as String?;
      if (m['path'] == 'subplaylist') playlistSha = m['sha'] as String?;
    }
    if (subSha == null) {
      throw Exception('"$_subdir" folder not found in $_owner/$_repo');
    }

    onProgress?.call('catalog', 'Reading the Sub-GHz catalog…', 0.4);

    // 2. Recursively list the Sub-GHz subtree (one call; ~11k entries fits well
    //    under the API's 100k / 7MB truncation limit).
    final subResp = await http
        .get(Uri.parse('$_treesApi/$subSha?recursive=1'), headers: _apiHeaders)
        .timeout(const Duration(seconds: 40));
    if (subResp.statusCode != 200) {
      throw Exception('GitHub subtree read failed: HTTP ${subResp.statusCode}');
    }
    final subJson = jsonDecode(subResp.body) as Map<String, dynamic>;
    final entries = <SubFileEntry>[];
    final playlists = <UberPlaylist>[];
    for (final t in ((subJson['tree'] as List?) ?? const [])) {
      final m = t as Map<String, dynamic>;
      if (m['type'] != 'blob') continue;
      final path = m['path'] as String? ?? '';
      if (UberPlaylists.isPlaylistPath('$_subdir/$path')) {
        playlists.add(UberPlaylist(repoPath: '$_subdir/$path'));
      }
      if (!path.toLowerCase().endsWith('.sub')) continue;
      entries.add(SubFileEntry(
        relativePath: path,
        content: Uint8List(0),
        size: (m['size'] as int?) ?? 0,
      ));
    }

    // 3. Uber's ready-made playlists (small folder; failure isn't fatal).
    if (playlistSha != null) {
      onProgress?.call('catalog', "Listing Uber's playlists…", 0.8);
      try {
        final r = await http
            .get(Uri.parse('$_treesApi/$playlistSha?recursive=1'), headers: _apiHeaders)
            .timeout(const Duration(seconds: 20));
        if (r.statusCode == 200) {
          for (final t in (((jsonDecode(r.body) as Map<String, dynamic>)['tree'] as List?) ??
              const [])) {
            final m = t as Map<String, dynamic>;
            final rp = 'subplaylist/${m['path']}';
            if (m['type'] == 'blob' && UberPlaylists.isPlaylistPath(rp)) {
              playlists.add(UberPlaylist(repoPath: rp));
            }
          }
        }
      } catch (_) {/* no playlists offered this time */}
    }
    playlists.sort((a, b) =>
        a.displayName.toLowerCase().compareTo(b.displayName.toLowerCase()));
    lastPlaylists = playlists;

    onProgress?.call('catalog', '${entries.length} .sub files available', 1.0);
    return entries;
  }

  /// Fetch the text of each playlist (small files, a few at a time). Ones that
  /// fail to download are dropped.
  static Future<List<UberPlaylist>> fetchPlaylistTexts(List<UberPlaylist> lists) async {
    final out = <UberPlaylist>[];
    for (var i = 0; i < lists.length; i += 6) {
      final batch = lists.skip(i).take(6);
      final got = await Future.wait(batch.map((p) async {
        try {
          final url = 'https://raw.githubusercontent.com/$_owner/$_repo/$_branch/'
              '${p.repoPath.split('/').map(Uri.encodeComponent).join('/')}';
          final r = await http
              .get(Uri.parse(url), headers: _apiHeaders)
              .timeout(const Duration(seconds: 20));
          if (r.statusCode == 200) return p.withText(utf8.decode(r.bodyBytes, allowMalformed: true));
        } catch (_) {}
        return null;
      }));
      out.addAll(got.whereType<UberPlaylist>());
    }
    return out;
  }

  /// Download the real bytes for the given [stubs] from raw.githubusercontent.
  /// Returns fully-populated [SubFileEntry]s; files that fail to download are
  /// skipped (not fatal). [onProgress] phase: "download".
  static Future<List<SubFileEntry>> downloadEntries(
    List<SubFileEntry> stubs, {
    void Function(String phase, String detail, double fraction)? onProgress,
  }) async {
    final out = <SubFileEntry>[];
    final total = stubs.length;
    for (var i = 0; i < total; i++) {
      final e = stubs[i];
      onProgress?.call(
        'download',
        'Downloading ${i + 1}/$total: ${e.relativePath}',
        total > 0 ? i / total : 0.0,
      );
      try {
        final resp = await http
            .get(Uri.parse(_rawUrl(e.relativePath)), headers: _apiHeaders)
            .timeout(const Duration(seconds: 20));
        if (resp.statusCode == 200) {
          out.add(SubFileEntry(
            relativePath: e.relativePath,
            content: resp.bodyBytes,
            size: resp.bodyBytes.length,
          ));
        }
      } catch (_) {
        // skip this file; keep going
      }
    }
    onProgress?.call('download', '${out.length} files downloaded', 1.0);
    return out;
  }

  /// raw.githubusercontent URL for a Sub-GHz-relative path, with each segment
  /// percent-encoded (UberGuidoZ paths contain spaces and punctuation).
  static String _rawUrl(String relativePath) {
    final encoded =
        relativePath.split('/').map(Uri.encodeComponent).join('/');
    return '$_rawBase$encoded';
  }

  // ---------------------------------------------------------------------------
  // Category selection — top-level folder is the selectable unit.
  // ---------------------------------------------------------------------------

  /// Top-level category of an entry (its first path segment), or [rootCategory]
  /// when the file is not inside a subfolder.
  static String categoryOf(String relativePath) {
    final slash = relativePath.indexOf('/');
    return slash > 0 ? relativePath.substring(0, slash) : rootCategory;
  }

  /// Group entries by top-level category folder -> file count, alphabetical.
  static Map<String, int> categoryCounts(List<SubFileEntry> files) {
    final counts = <String, int>{};
    for (final f in files) {
      final category = categoryOf(f.relativePath);
      counts[category] = (counts[category] ?? 0) + 1;
    }
    final entries = counts.entries.toList()
      ..sort((a, b) => a.key.toLowerCase().compareTo(b.key.toLowerCase()));
    return Map<String, int>.fromEntries(entries);
  }

  /// True if any path segment looks like a brute-force dump folder.
  static bool isBruteForcePath(String relativePath) {
    for (final seg in relativePath.toLowerCase().split('/')) {
      if (seg.contains('brute')) return true;
    }
    return false;
  }

  /// Filter entries to the user's selection: only files whose top-level
  /// category is in [categories], optionally dropping brute-force folders.
  static List<SubFileEntry> filterSelection(
    List<SubFileEntry> files, {
    required Set<String> categories,
    required bool excludeBruteForce,
  }) {
    return files.where((f) {
      if (!categories.contains(categoryOf(f.relativePath))) return false;
      if (excludeBruteForce && isBruteForcePath(f.relativePath)) return false;
      return true;
    }).toList();
  }

  // ---------------------------------------------------------------------------
  // Large-file splitting — big RAW .sub sweeps time out over BLE. Split them
  // into smaller, fully-valid .sub pieces. (Unchanged logic.)
  // ---------------------------------------------------------------------------

  // Anything over the Crow's per-file upload limit is split; pieces stay well
  // under it (header included).
  static const int splitThresholdBytes = FirmwareBinaryProtocol.MAX_UPLOAD_BYTES;
  static const int splitPieceTargetBytes = 100 * 1024;

  static List<SubFileEntry> splitIfLargeRaw(SubFileEntry entry) {
    if (entry.content.length <= splitThresholdBytes) return [entry];

    final lines = String.fromCharCodes(entry.content).split('\n');

    final headerLines = <String>[];
    int firstRaw = -1;
    for (int i = 0; i < lines.length; i++) {
      if (lines[i].startsWith('RAW_Data:')) {
        firstRaw = i;
        break;
      }
      headerLines.add(lines[i]);
    }
    final isRaw = headerLines.any(
        (l) => l.replaceAll(' ', '').toUpperCase().startsWith('PROTOCOL:RAW'));
    if (firstRaw < 0 || !isRaw) return [entry];

    final rawLines =
        lines.sublist(firstRaw).where((l) => l.startsWith('RAW_Data:')).toList();
    if (rawLines.isEmpty) return [entry];

    final header = headerLines.join('\n');
    final int headerBytes = header.length + 1;

    final pieces = <List<String>>[];
    var current = <String>[];
    int currentBytes = headerBytes;
    for (final line in rawLines) {
      final lineBytes = line.length + 1;
      if (current.isNotEmpty &&
          currentBytes + lineBytes > splitPieceTargetBytes) {
        pieces.add(current);
        current = <String>[];
        currentBytes = headerBytes;
      }
      current.add(line);
      currentBytes += lineBytes;
    }
    if (current.isNotEmpty) pieces.add(current);
    if (pieces.length <= 1) return [entry];

    final base = entry.relativePath.toLowerCase().endsWith('.sub')
        ? entry.relativePath.substring(0, entry.relativePath.length - 4)
        : entry.relativePath;
    final out = <SubFileEntry>[];
    for (int i = 0; i < pieces.length; i++) {
      final nn = (i + 1).toString().padLeft(2, '0');
      final body = '$header\n${pieces[i].join('\n')}\n';
      out.add(SubFileEntry(
        relativePath: '${base}_$nn.sub',
        content: Uint8List.fromList(body.codeUnits),
      ));
    }
    return out;
  }

  /// Expand an upload selection, splitting any oversized RAW .sub into pieces.
  static List<SubFileEntry> expandForUpload(List<SubFileEntry> files) {
    final out = <SubFileEntry>[];
    for (final f in files) {
      out.addAll(splitIfLargeRaw(f));
    }
    return out;
  }

  // ---------------------------------------------------------------------------
  // Progress persistence for Pause / Resume (upload progress + the selection).
  // ---------------------------------------------------------------------------

  static const String _progressFileName = 'clone_progress.json';
  static const String _selectionFileName = 'clone_selection.json';

  static Future<Directory> _cacheDir() async {
    final appDir = await getApplicationDocumentsDirectory();
    final dir = Directory('${appDir.path}/clone_cache');
    if (!await dir.exists()) await dir.create(recursive: true);
    return dir;
  }

  /// A resumable session exists if we have saved upload progress.
  static Future<bool> hasResumableSession() async {
    final dir = await _cacheDir();
    return File('${dir.path}/$_progressFileName').exists();
  }

  static Future<Set<String>> loadCompletedFiles() async {
    final dir = await _cacheDir();
    final file = File('${dir.path}/$_progressFileName');
    if (!await file.exists()) return {};
    try {
      final json = jsonDecode(await file.readAsString());
      return Set<String>.from(json['completed'] as List);
    } catch (_) {
      return {};
    }
  }

  static Future<void> saveProgress(Set<String> completedPaths) async {
    final dir = await _cacheDir();
    final file = File('${dir.path}/$_progressFileName');
    await file.writeAsString(jsonEncode({'completed': completedPaths.toList()}));
  }

  /// Persist the folder selection so a resumed session re-downloads the same
  /// folders before continuing the upload.
  static Future<void> saveSelection(
      Set<String> categories, bool includeBruteForce,
      {Set<String> playlists = const {}}) async {
    final dir = await _cacheDir();
    final file = File('${dir.path}/$_selectionFileName');
    await file.writeAsString(jsonEncode({
      'categories': categories.toList(),
      'includeBruteForce': includeBruteForce,
      'playlists': playlists.toList(),
    }));
  }

  /// Load the saved selection, or null if none.
  static Future<({Set<String> categories, bool includeBruteForce, Set<String> playlists})?>
      loadSelection() async {
    final dir = await _cacheDir();
    final file = File('${dir.path}/$_selectionFileName');
    if (!await file.exists()) return null;
    try {
      final json = jsonDecode(await file.readAsString()) as Map<String, dynamic>;
      return (
        categories: Set<String>.from(json['categories'] as List),
        includeBruteForce: (json['includeBruteForce'] as bool?) ?? false,
        playlists: Set<String>.from((json['playlists'] as List?) ?? const []),
      );
    } catch (_) {
      return null;
    }
  }

  static Future<void> clearCache() async {
    final dir = await _cacheDir();
    if (await dir.exists()) await dir.delete(recursive: true);
  }
}

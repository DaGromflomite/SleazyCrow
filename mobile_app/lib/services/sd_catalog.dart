import 'dart:convert';
import 'dart:io';

import 'package:path_provider/path_provider.dart';

import '../models/file_item.dart';

/// The phone's own list of the .sub files on the Crow's SD card, so playlists
/// can be built with no Crow attached.
///
/// Kept up to date automatically:
///  - every successful .sub upload adds its path (this covers the downloader);
///  - every folder listing from the Crow replaces what we knew about that
///    folder (so deletes, renames and moves made elsewhere get corrected);
///  - a successful SD format empties it.
///
/// Paths are SD-absolute ("/SUB Files/Gates/x.sub"), the same form playlist
/// items use with pathType 5. It's a best-effort snapshot: the card can change
/// while the phone isn't looking (e.g. edited on a PC), which is why playlists
/// report files that fail to play.
class SdCatalog {
  SdCatalog._();

  static const _fileName = 'sd_catalog.json';
  static final Set<String> _files = {};
  static DateTime? _updated;
  static bool _loaded = false;
  static Future<void>? _saving;
  static bool _dirty = false;

  static const Map<int, String> _bases = {
    0: '/DATA/RECORDS',
    1: '/DATA/SIGNALS',
    2: '/DATA/PRESETS',
    3: '/DATA/TEMP',
  };

  // ---- pure helpers (unit-tested) -------------------------------------------

  /// SD-absolute form of a path the app uses with [pathType]. Returns '' for
  /// LittleFS (pathType 4), which isn't on the SD card.
  static String toSdPath(String path, int pathType) {
    if (pathType == 4) return '';
    var p = path.replaceAll('\\', '/');
    final base = _bases[pathType];
    if (base != null && !p.startsWith('/DATA/') && p != '/DATA') p = '$base/$p';
    if (!p.startsWith('/')) p = '/$p';
    p = p.replaceAll(RegExp(r'/+'), '/');
    if (p.length > 1 && p.endsWith('/')) p = p.substring(0, p.length - 1);
    return p;
  }

  static bool isSub(String path) => path.toLowerCase().endsWith('.sub');

  static String parentOf(String path) {
    final i = path.lastIndexOf('/');
    return i <= 0 ? '/' : path.substring(0, i);
  }

  /// [files] with the contents of [folder] replaced by a fresh listing:
  /// direct .sub files become [subNames]; anything under a subfolder that is
  /// no longer listed in [dirNames] is dropped.
  static Set<String> applyListing(
      Set<String> files, String folder, Iterable<String> dirNames, Iterable<String> subNames) {
    final f = folder == '/' ? '' : folder;
    final dirs = dirNames.toSet();
    final out = <String>{};
    for (final p in files) {
      if (!p.startsWith('$f/')) {
        out.add(p);
        continue;
      }
      final rest = p.substring(f.length + 1);
      final slash = rest.indexOf('/');
      if (slash < 0) continue; // direct child: replaced below
      if (dirs.contains(rest.substring(0, slash))) out.add(p); // still-listed subfolder
    }
    for (final n in subNames) {
      if (isSub(n)) out.add('$f/$n');
    }
    return out;
  }

  /// Subfolder names and .sub file paths directly inside [folder].
  static ({List<String> dirs, List<String> files}) children(Set<String> all, String folder) {
    final f = folder == '/' ? '' : folder;
    final dirs = <String>{};
    final files = <String>[];
    for (final p in all) {
      if (!p.startsWith('$f/')) continue;
      final rest = p.substring(f.length + 1);
      final slash = rest.indexOf('/');
      if (slash < 0) {
        files.add(p);
      } else {
        dirs.add(rest.substring(0, slash));
      }
    }
    final d = dirs.toList()..sort((a, b) => a.toLowerCase().compareTo(b.toLowerCase()));
    files.sort((a, b) => a.toLowerCase().compareTo(b.toLowerCase()));
    return (dirs: d, files: files);
  }

  // ---- stored state -----------------------------------------------------------

  static Future<File> _file() async {
    final dir = await getApplicationDocumentsDirectory();
    return File('${dir.path}/$_fileName');
  }

  static Future<void> _ensureLoaded() async {
    if (_loaded) return;
    _loaded = true;
    try {
      final f = await _file();
      if (await f.exists()) {
        final j = jsonDecode(await f.readAsString()) as Map<String, dynamic>;
        _files.addAll(((j['files'] as List?) ?? const []).cast<String>());
        final u = j['updated'] as String?;
        _updated = u == null ? null : DateTime.tryParse(u);
      }
    } catch (_) {/* damaged file: start empty */}
  }

  static void _changed() {
    _updated = DateTime.now();
    _dirty = true;
    _saving ??= Future.delayed(const Duration(seconds: 2), () async {
      _saving = null;
      if (!_dirty) return;
      _dirty = false;
      try {
        final f = await _file();
        await f.writeAsString(jsonEncode({
          'updated': _updated?.toIso8601String(),
          'files': _files.toList(),
        }));
      } catch (_) {/* best effort */}
    });
  }

  /// All known .sub paths and when the list last changed.
  static Future<({Set<String> files, DateTime? updated})> snapshot() async {
    await _ensureLoaded();
    return (files: Set<String>.of(_files), updated: _updated);
  }

  /// A file was uploaded to the Crow.
  static Future<void> addUpload(String path, int pathType) async {
    final p = toSdPath(path, pathType);
    if (p.isEmpty || !isSub(p)) return;
    await _ensureLoaded();
    if (_files.add(p)) _changed();
  }

  /// The Crow listed [folder] (in the app's [pathType] form).
  static Future<void> syncListing(String folder, int pathType, List<FileItem> items) async {
    final f = toSdPath(folder, pathType);
    if (f.isEmpty) return;
    await _ensureLoaded();
    final next = applyListing(
      _files,
      f,
      items.where((i) => i.isDirectory).map((i) => i.name),
      items.where((i) => i.isFile).map((i) => i.name),
    );
    if (next.length != _files.length || !next.containsAll(_files)) {
      _files
        ..clear()
        ..addAll(next);
      _changed();
    }
  }

  /// The SD card was formatted.
  static Future<void> clear() async {
    await _ensureLoaded();
    _files.clear();
    _changed();
  }
}

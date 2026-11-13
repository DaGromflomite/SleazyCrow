import 'dart:convert';
import 'package:shared_preferences/shared_preferences.dart';
import '../models/sub_playlist.dart';

/// Persists Sub-GHz transmit playlists app-side as a JSON array in
/// SharedPreferences. No firmware involvement — purely the phone remembering
/// which saved .sub files the user grouped together.
class PlaylistService {
  static const _key = 'subghz_playlists_v1';

  /// Load all saved playlists (empty list if none / on any parse error).
  static Future<List<SubPlaylist>> load() async {
    try {
      final prefs = await SharedPreferences.getInstance();
      final raw = prefs.getString(_key);
      if (raw == null || raw.isEmpty) return [];
      final list = jsonDecode(raw) as List;
      return list
          .map((e) => SubPlaylist.fromJson(e as Map<String, dynamic>))
          .toList();
    } catch (_) {
      return [];
    }
  }

  /// Persist the full set of playlists, replacing what was there.
  static Future<void> saveAll(List<SubPlaylist> playlists) async {
    final prefs = await SharedPreferences.getInstance();
    final raw = jsonEncode(playlists.map((p) => p.toJson()).toList());
    await prefs.setString(_key, raw);
  }
}

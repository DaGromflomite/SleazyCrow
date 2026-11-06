import 'dart:convert';
import 'dart:typed_data';

import 'package:flutter/material.dart';
import 'package:provider/provider.dart';
import 'package:shared_preferences/shared_preferences.dart';

import '../models/file_item.dart';
import '../models/sub_playlist.dart';
import '../providers/ble_provider.dart';
import '../services/playlist_service.dart';
import '../theme/app_colors.dart';
import '../widgets/logo_watermark.dart';

/// Folder on the Crow's SD card (pathType 5 root) where exported playlists go.
const String kPlaylistSdFolder = 'PLAYLISTS';

/// Sub-GHz transmit playlists: build an ordered list of saved .sub files and
/// transmit them in sequence (gap, repeat-each, loop). App-side only — playback
/// reuses the normal transmit path, paced on the firmware burst-complete event.
class PlaylistScreen extends StatefulWidget {
  const PlaylistScreen({super.key});

  @override
  State<PlaylistScreen> createState() => _PlaylistScreenState();
}

class _PlaylistScreenState extends State<PlaylistScreen> {
  List<SubPlaylist> _playlists = [];
  bool _loading = true;

  @override
  void initState() {
    super.initState();
    _load();
  }

  Future<void> _load() async {
    final p = await PlaylistService.load();
    if (!mounted) return;
    setState(() {
      _playlists = p;
      _loading = false;
    });
    _maybeShowIntro();
  }

  /// One-time explainer: playlists live on the phone, but can be exported to
  /// the Crow's SD card. Shown the first time this tab is opened.
  Future<void> _maybeShowIntro() async {
    const key = 'playlist_intro_seen_v1';
    final prefs = await SharedPreferences.getInstance();
    if (prefs.getBool(key) == true) return;
    if (!mounted) return;
    WidgetsBinding.instance.addPostFrameCallback((_) async {
      if (!mounted) return;
      await showDialog<void>(
        context: context,
        builder: (c) => AlertDialog(
          backgroundColor: AppColors.secondaryBackground,
          title: Row(
            children: const [
              Icon(Icons.queue_music, color: AppColors.cyberPink),
              SizedBox(width: 10),
              Text('About playlists',
                  style: TextStyle(color: AppColors.primaryText)),
            ],
          ),
          content: const Text(
            'Playlists are saved on your phone, so you can build and edit them '
            'with no Crow attached. Each playlist just points at .sub files on '
            'the Crow\'s SD card.\n\n'
            'If you want a backup or to move a playlist, use Export to write it '
            'to a /PLAYLISTS folder on the Crow\'s SD card (Crow must be '
            'connected).',
            style: TextStyle(color: AppColors.secondaryText),
          ),
          actions: [
            TextButton(
              onPressed: () => Navigator.pop(c),
              child: const Text('Got it',
                  style: TextStyle(color: AppColors.cyberPink)),
            ),
          ],
        ),
      );
      await prefs.setBool(key, true);
    });
  }

  Future<void> _export(SubPlaylist pl) async {
    final ble = Provider.of<BleProvider>(context, listen: false);
    final messenger = ScaffoldMessenger.of(context);
    if (!ble.isConnected) {
      messenger.showSnackBar(const SnackBar(
        content: Text('Connect your Crow to export to its SD card.'),
        backgroundColor: AppColors.surfaceElevated,
      ));
      return;
    }
    messenger.showSnackBar(SnackBar(
      content: Text('Exporting "${pl.name}" to SD…'),
      backgroundColor: AppColors.surfaceElevated,
    ));
    try {
      // Ensure /PLAYLISTS exists (ignore "already exists").
      await ble.createDirectory(kPlaylistSdFolder, pathType: 5);
      final safe =
          pl.name.replaceAll(RegExp(r'[^A-Za-z0-9 _-]'), '_').trim();
      final target = '/$kPlaylistSdFolder/${safe.isEmpty ? 'playlist' : safe}.json';
      final bytes = Uint8List.fromList(
          utf8.encode(const JsonEncoder.withIndent('  ').convert(pl.toJson())));
      await ble.uploadFileFromBytes(bytes, target, pathType: 5);
      messenger.showSnackBar(SnackBar(
        content: Text('Exported to $target'),
        backgroundColor: AppColors.surfaceElevated,
      ));
    } catch (e) {
      messenger.showSnackBar(SnackBar(
        content: Text('Export failed: $e'),
        backgroundColor: AppColors.surfaceElevated,
      ));
    }
  }

  Future<void> _persist() => PlaylistService.saveAll(_playlists);

  Future<void> _newPlaylist() async {
    final name = await _promptName(context, title: 'New playlist');
    if (name == null || name.trim().isEmpty) return;
    final pl = SubPlaylist(name: name.trim());
    setState(() => _playlists.add(pl));
    await _persist();
    if (!mounted) return;
    _openDetail(pl);
  }

  Future<void> _rename(SubPlaylist pl) async {
    final name = await _promptName(context, title: 'Rename', initial: pl.name);
    if (name == null || name.trim().isEmpty) return;
    setState(() => pl.name = name.trim());
    await _persist();
  }

  Future<void> _delete(SubPlaylist pl) async {
    final ok = await showDialog<bool>(
      context: context,
      builder: (c) => AlertDialog(
        backgroundColor: AppColors.secondaryBackground,
        title: const Text('Delete playlist?',
            style: TextStyle(color: AppColors.primaryText)),
        content: Text('"${pl.name}" will be removed.',
            style: const TextStyle(color: AppColors.secondaryText)),
        actions: [
          TextButton(
              onPressed: () => Navigator.pop(c, false),
              child: const Text('Cancel')),
          TextButton(
              onPressed: () => Navigator.pop(c, true),
              child: const Text('Delete',
                  style: TextStyle(color: AppColors.error))),
        ],
      ),
    );
    if (ok == true) {
      setState(() => _playlists.remove(pl));
      await _persist();
    }
  }

  Future<void> _openDetail(SubPlaylist pl) async {
    await Navigator.of(context).push(
      MaterialPageRoute(
        builder: (_) => _PlaylistDetailScreen(playlist: pl, onChanged: _persist),
      ),
    );
    if (mounted) setState(() {}); // reflect edits (count etc.)
    await _persist();
  }

  @override
  Widget build(BuildContext context) {
    if (_loading) {
      return const Center(
          child: CircularProgressIndicator(color: AppColors.cyberPink));
    }
    return Column(
      children: [
        Expanded(
          child: _playlists.isEmpty
              ? _empty()
              : ListView.builder(
                  padding: const EdgeInsets.fromLTRB(12, 10, 12, 12),
                  itemCount: _playlists.length,
                  itemBuilder: (c, i) => _playlistCard(_playlists[i]),
                ),
        ),
        if (_playlists.isNotEmpty)
          SafeArea(
            top: false,
            child: Padding(
              padding: const EdgeInsets.fromLTRB(12, 4, 12, 12),
              child: SizedBox(width: double.infinity, child: _newButton()),
            ),
          ),
      ],
    );
  }

  Widget _empty() => Center(
        child: Stack(
          alignment: Alignment.center,
          children: [
            const LogoWatermark(size: 260, opacity: 0.06),
            Padding(
              padding: const EdgeInsets.all(28),
              child: Column(
                mainAxisSize: MainAxisSize.min,
                children: [
                  const Text('No playlists yet',
                      style: TextStyle(
                          color: AppColors.primaryText,
                          fontSize: 16,
                          fontWeight: FontWeight.w600)),
                  const SizedBox(height: 6),
                  const Text(
                    'Build an ordered list of saved .sub files and transmit them in sequence.',
                    textAlign: TextAlign.center,
                    style:
                        TextStyle(color: AppColors.secondaryText, fontSize: 13),
                  ),
                  const SizedBox(height: 18),
                  _newButton(),
                ],
              ),
            ),
          ],
        ),
      );

  Widget _playlistCard(SubPlaylist pl) {
    return Card(
      color: AppColors.secondaryBackground,
      margin: const EdgeInsets.only(bottom: 10),
      shape: RoundedRectangleBorder(
        borderRadius: BorderRadius.circular(12),
        side: const BorderSide(color: AppColors.borderDefault),
      ),
      child: ListTile(
        leading: const Icon(Icons.queue_music, color: AppColors.cyberPink),
        title: Text(pl.name,
            style: const TextStyle(
                color: AppColors.primaryText, fontWeight: FontWeight.w600)),
        subtitle: Text(
          () {
            final files = pl.fileCount;
            final pauses = pl.items.length - files;
            return '$files file${files == 1 ? '' : 's'}'
                '${pauses > 0 ? ' · $pauses pause${pauses == 1 ? '' : 's'}' : ''}'
                '${pl.loop ? ' · loop' : ''}';
          }(),
          style: const TextStyle(color: AppColors.secondaryText, fontSize: 12),
        ),
        trailing: PopupMenuButton<String>(
          color: AppColors.surfaceElevated,
          icon: const Icon(Icons.more_vert, color: AppColors.secondaryText),
          onSelected: (v) {
            if (v == 'rename') _rename(pl);
            if (v == 'export') _export(pl);
            if (v == 'delete') _delete(pl);
          },
          itemBuilder: (_) => const [
            PopupMenuItem(
                value: 'rename',
                child: Text('Rename',
                    style: TextStyle(color: AppColors.primaryText))),
            PopupMenuItem(
                value: 'export',
                child: Text('Export to SD',
                    style: TextStyle(color: AppColors.primaryText))),
            PopupMenuItem(
                value: 'delete',
                child:
                    Text('Delete', style: TextStyle(color: AppColors.error))),
          ],
        ),
        onTap: () => _openDetail(pl),
      ),
    );
  }

  Widget _newButton() => ElevatedButton.icon(
        style: ElevatedButton.styleFrom(
          backgroundColor: AppColors.cyberPink,
          foregroundColor: Colors.white,
          padding: const EdgeInsets.symmetric(horizontal: 20, vertical: 12),
        ),
        onPressed: _newPlaylist,
        icon: const Icon(Icons.add),
        label: const Text('New playlist'),
      );
}

/// Name-entry dialog shared by create/rename.
Future<String?> _promptName(BuildContext context,
    {required String title, String? initial}) {
  final ctrl = TextEditingController(text: initial ?? '');
  return showDialog<String>(
    context: context,
    builder: (c) => AlertDialog(
      backgroundColor: AppColors.secondaryBackground,
      title: Text(title, style: const TextStyle(color: AppColors.primaryText)),
      content: TextField(
        controller: ctrl,
        autofocus: true,
        style: const TextStyle(color: AppColors.primaryText),
        decoration: const InputDecoration(
          hintText: 'Playlist name',
          hintStyle: TextStyle(color: AppColors.disabledText),
          enabledBorder: UnderlineInputBorder(
              borderSide: BorderSide(color: AppColors.borderDefault)),
          focusedBorder: UnderlineInputBorder(
              borderSide: BorderSide(color: AppColors.cyberPink)),
        ),
        onSubmitted: (v) => Navigator.pop(c, v),
      ),
      actions: [
        TextButton(
            onPressed: () => Navigator.pop(c, null),
            child: const Text('Cancel')),
        TextButton(
            onPressed: () => Navigator.pop(c, ctrl.text),
            child: const Text('OK',
                style: TextStyle(color: AppColors.cyberPink))),
      ],
    ),
  );
}

// ---------------------------------------------------------------------------
// Detail: editor + player for one playlist.
// ---------------------------------------------------------------------------

class _PlaylistDetailScreen extends StatefulWidget {
  final SubPlaylist playlist;
  final Future<void> Function() onChanged;
  const _PlaylistDetailScreen({required this.playlist, required this.onChanged});

  @override
  State<_PlaylistDetailScreen> createState() => _PlaylistDetailScreenState();
}

class _PlaylistDetailScreenState extends State<_PlaylistDetailScreen> {
  SubPlaylist get pl => widget.playlist;

  Future<void> _save() async {
    await widget.onChanged();
  }

  Future<void> _renameHere() async {
    final name =
        await _promptName(context, title: 'Rename playlist', initial: pl.name);
    if (name == null || name.trim().isEmpty) return;
    setState(() => pl.name = name.trim());
    await _save();
  }

  Future<void> _saveAndClose() async {
    await _save(); // already saved on each edit; this confirms + exits
    if (!mounted) return;
    ScaffoldMessenger.of(context).showSnackBar(SnackBar(
      content: Text('Saved "${pl.name}"'),
      backgroundColor: AppColors.surfaceElevated,
      duration: const Duration(seconds: 1),
    ));
    Navigator.of(context).pop();
  }

  Future<void> _addFromSd() async {
    final ble = Provider.of<BleProvider>(context, listen: false);
    if (!ble.isConnected) {
      _snack('Connect your Crow to browse its SD card.');
      return;
    }
    final picked = await showModalBottomSheet<List<PlaylistItem>>(
      context: context,
      backgroundColor: AppColors.primaryBackground,
      isScrollControlled: true,
      showDragHandle: true,
      builder: (_) => const _SubFilePicker(),
    );
    if (picked != null && picked.isNotEmpty) {
      setState(() => pl.items.addAll(picked));
      await _save();
    }
  }

  Future<void> _addPause() async {
    final ms = await showModalBottomSheet<int>(
      context: context,
      backgroundColor: AppColors.secondaryBackground,
      showDragHandle: true,
      builder: (c) {
        const presets = <int>[250, 500, 1000, 2000, 3000, 5000, 10000];
        return SafeArea(
          child: Column(
            mainAxisSize: MainAxisSize.min,
            children: [
              const Padding(
                padding: EdgeInsets.fromLTRB(20, 4, 20, 10),
                child: Align(
                  alignment: Alignment.centerLeft,
                  child: Text('Add a pause',
                      style: TextStyle(
                          color: AppColors.primaryText,
                          fontSize: 18,
                          fontWeight: FontWeight.bold)),
                ),
              ),
              Wrap(
                spacing: 10,
                runSpacing: 10,
                children: [
                  for (final p in presets)
                    OutlinedButton(
                      style: OutlinedButton.styleFrom(
                        foregroundColor: AppColors.cyanAccent,
                        side: const BorderSide(color: AppColors.cyanAccent),
                      ),
                      onPressed: () => Navigator.pop(c, p),
                      child: Text(p < 1000
                          ? '$p ms'
                          : '${(p / 1000).toStringAsFixed(p % 1000 == 0 ? 0 : 1)} s'),
                    ),
                ],
              ),
              const SizedBox(height: 16),
            ],
          ),
        );
      },
    );
    if (ms != null) {
      setState(() => pl.items.add(PlaylistItem.pause(ms)));
      await _save();
    }
  }

  void _snack(String msg) {
    if (!mounted) return;
    ScaffoldMessenger.of(context).showSnackBar(SnackBar(
      content: Text(msg),
      backgroundColor: AppColors.surfaceElevated,
    ));
  }

  Future<void> _togglePlay(BleProvider ble) async {
    if (ble.isPlaylistPlaying) {
      ble.stopPlaylist();
      return;
    }
    if (!ble.isConnected) {
      _snack('Connect your Crow to transmit.');
      return;
    }
    if (pl.items.isEmpty) {
      _snack('Add some files first.');
      return;
    }
    await ble.startPlaylist(
      List<PlaylistItem>.from(pl.items),
      gapMs: pl.gapMs,
      repeatEach: pl.repeatEach,
      loop: pl.loop,
    );
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      backgroundColor: AppColors.primaryBackground,
      appBar: AppBar(
        backgroundColor: AppColors.secondaryBackground,
        foregroundColor: AppColors.primaryText,
        title: GestureDetector(
          onTap: _renameHere,
          child: Row(
            mainAxisSize: MainAxisSize.min,
            children: [
              Flexible(
                  child: Text(pl.name, overflow: TextOverflow.ellipsis)),
              const SizedBox(width: 6),
              const Icon(Icons.edit, size: 16, color: AppColors.secondaryText),
            ],
          ),
        ),
        actions: [
          TextButton.icon(
            onPressed: _saveAndClose,
            icon: const Icon(Icons.check, color: AppColors.cyberPink),
            label: const Text('Save',
                style: TextStyle(
                    color: AppColors.cyberPink, fontWeight: FontWeight.w600)),
          ),
        ],
      ),
      body: Consumer<BleProvider>(
        builder: (context, ble, _) {
          final playing = ble.isPlaylistPlaying;
          return Column(
            children: [
              Expanded(
                child: pl.items.isEmpty
                    ? _emptyItems()
                    : ReorderableListView.builder(
                        padding: const EdgeInsets.fromLTRB(12, 8, 12, 8),
                        itemCount: pl.items.length,
                        onReorder: (oldI, newI) async {
                          setState(() {
                            if (newI > oldI) newI -= 1;
                            final it = pl.items.removeAt(oldI);
                            pl.items.insert(newI, it);
                          });
                          await _save();
                        },
                        itemBuilder: (c, i) => _itemTile(ble, i, playing),
                      ),
              ),
              _controls(ble, playing),
            ],
          );
        },
      ),
    );
  }

  Widget _emptyItems() => const LogoEmptyState(
        title: 'Empty playlist',
        message:
            'Tap "Add from SD" to pick saved .sub files, or "Add pause" to space them out.',
      );

  Widget _itemTile(BleProvider ble, int i, bool playing) {
    final item = pl.items[i];
    final isCurrent = playing && ble.playlistIndex == i;
    final isPause = item.isPause;
    return Container(
      key: ValueKey('item_${i}_${item.kind}_${item.path}_${item.pauseMs}'),
      margin: const EdgeInsets.only(bottom: 8),
      decoration: BoxDecoration(
        color: isCurrent
            ? AppColors.cyberPink.withValues(alpha: 0.14)
            : (isPause
                ? AppColors.primaryBackground
                : AppColors.secondaryBackground),
        borderRadius: BorderRadius.circular(10),
        border: Border.all(
            color: isCurrent
                ? AppColors.cyberPink
                : (isPause ? AppColors.divider : AppColors.borderDefault)),
      ),
      child: ListTile(
        dense: true,
        leading: isCurrent
            ? const Icon(Icons.graphic_eq, color: AppColors.cyberPink)
            : (isPause
                ? const Icon(Icons.pause_circle_outline,
                    color: AppColors.cyanAccent)
                : Text('${i + 1}',
                    style: const TextStyle(color: AppColors.secondaryText))),
        title: Text(item.displayName,
            maxLines: 1,
            overflow: TextOverflow.ellipsis,
            style: TextStyle(
                color: isPause
                    ? AppColors.secondaryText
                    : AppColors.primaryText,
                fontStyle: isPause ? FontStyle.italic : FontStyle.normal,
                fontSize: 14)),
        subtitle: isPause
            ? null
            : Text(item.path,
                maxLines: 1,
                overflow: TextOverflow.ellipsis,
                style: const TextStyle(
                    color: AppColors.disabledText, fontSize: 11)),
        trailing: Row(
          mainAxisSize: MainAxisSize.min,
          children: [
            IconButton(
              icon: const Icon(Icons.delete_outline,
                  color: AppColors.secondaryText, size: 20),
              onPressed: playing
                  ? null
                  : () async {
                      setState(() => pl.items.removeAt(i));
                      await _save();
                    },
            ),
            const Icon(Icons.drag_handle, color: AppColors.disabledText),
          ],
        ),
      ),
    );
  }

  Widget _controls(BleProvider ble, bool playing) {
    return Container(
      padding: const EdgeInsets.fromLTRB(16, 12, 16, 18),
      decoration: const BoxDecoration(
        color: AppColors.secondaryBackground,
        border: Border(top: BorderSide(color: AppColors.borderDefault)),
      ),
      child: Column(
        mainAxisSize: MainAxisSize.min,
        children: [
          // Gap slider
          Row(
            children: [
              const SizedBox(
                  width: 92,
                  child: Text('Gap',
                      style: TextStyle(color: AppColors.secondaryText))),
              Expanded(
                child: Slider(
                  value: pl.gapMs.toDouble().clamp(0, 3000),
                  min: 0,
                  max: 3000,
                  divisions: 30,
                  activeColor: AppColors.cyberPink,
                  label: '${pl.gapMs} ms',
                  onChanged: playing
                      ? null
                      : (v) => setState(() => pl.gapMs = v.round()),
                  onChangeEnd: (_) => _save(),
                ),
              ),
              SizedBox(
                  width: 56,
                  child: Text('${pl.gapMs}ms',
                      textAlign: TextAlign.right,
                      style: const TextStyle(
                          color: AppColors.primaryText, fontSize: 12))),
            ],
          ),
          // Repeat-each + loop
          Row(
            children: [
              const SizedBox(
                  width: 92,
                  child: Text('Repeat each',
                      style: TextStyle(color: AppColors.secondaryText))),
              IconButton(
                icon: const Icon(Icons.remove_circle_outline,
                    color: AppColors.secondaryText),
                onPressed: playing || pl.repeatEach <= 1
                    ? null
                    : () async {
                        setState(() => pl.repeatEach -= 1);
                        await _save();
                      },
              ),
              Text('${pl.repeatEach}×',
                  style: const TextStyle(
                      color: AppColors.primaryText,
                      fontWeight: FontWeight.w600)),
              IconButton(
                icon: const Icon(Icons.add_circle_outline,
                    color: AppColors.secondaryText),
                onPressed: playing || pl.repeatEach >= 20
                    ? null
                    : () async {
                        setState(() => pl.repeatEach += 1);
                        await _save();
                      },
              ),
              const Spacer(),
              const Text('Loop',
                  style: TextStyle(color: AppColors.secondaryText)),
              Switch(
                value: pl.loop,
                activeThumbColor: AppColors.cyberPink,
                onChanged: playing
                    ? null
                    : (v) async {
                        setState(() => pl.loop = v);
                        await _save();
                      },
              ),
            ],
          ),
          const SizedBox(height: 6),
          Row(
            children: [
              Expanded(
                child: OutlinedButton.icon(
                  style: OutlinedButton.styleFrom(
                    foregroundColor: AppColors.cyberPink,
                    side: const BorderSide(color: AppColors.cyberPink),
                    padding: const EdgeInsets.symmetric(vertical: 12),
                  ),
                  onPressed: playing ? null : _addFromSd,
                  icon: const Icon(Icons.sd_card, size: 18),
                  label: const Text('Add from SD'),
                ),
              ),
              const SizedBox(width: 12),
              Expanded(
                child: OutlinedButton.icon(
                  style: OutlinedButton.styleFrom(
                    foregroundColor: AppColors.cyanAccent,
                    side: const BorderSide(color: AppColors.cyanAccent),
                    padding: const EdgeInsets.symmetric(vertical: 12),
                  ),
                  onPressed: playing ? null : _addPause,
                  icon: const Icon(Icons.more_time, size: 18),
                  label: const Text('Add pause'),
                ),
              ),
            ],
          ),
          const SizedBox(height: 10),
          SizedBox(
            width: double.infinity,
            child: ElevatedButton.icon(
              style: ElevatedButton.styleFrom(
                backgroundColor:
                    playing ? AppColors.error : AppColors.cyberPink,
                foregroundColor: Colors.white,
                padding: const EdgeInsets.symmetric(vertical: 14),
              ),
              onPressed: () => _togglePlay(ble),
              icon: Icon(playing ? Icons.stop : Icons.play_arrow),
              label: Text(playing
                  ? 'Stop'
                  : (ble.playlistPass > 0 ? 'Play again' : 'Play')),
            ),
          ),
          if (playing)
            Padding(
              padding: const EdgeInsets.only(top: 8),
              child: Text(
                'Transmitting ${ble.playlistIndex + 1} of ${pl.items.length}'
                '${pl.loop ? '  ·  loop ${ble.playlistPass + 1}' : ''}',
                style:
                    const TextStyle(color: AppColors.cyberPink, fontSize: 12),
              ),
            ),
        ],
      ),
    );
  }
}

// ---------------------------------------------------------------------------
// SD file picker: browse the Crow's SD (pathType 5) and multi-select .sub files.
// ---------------------------------------------------------------------------

class _SubFilePicker extends StatefulWidget {
  const _SubFilePicker();

  @override
  State<_SubFilePicker> createState() => _SubFilePickerState();
}

class _SubFilePickerState extends State<_SubFilePicker> {
  final Set<String> _selected = {};
  bool _busy = true;

  @override
  void initState() {
    super.initState();
    _openRoot();
  }

  Future<void> _openRoot() async {
    final ble = Provider.of<BleProvider>(context, listen: false);
    setState(() => _busy = true);
    try {
      await ble.switchPathType(5); // SD root
      await ble.refreshFileList(forceRefresh: true, pathType: 5);
    } catch (_) {}
    if (mounted) setState(() => _busy = false);
  }

  Future<void> _enter(String dir) async {
    final ble = Provider.of<BleProvider>(context, listen: false);
    setState(() => _busy = true);
    try {
      await ble.navigateToDirectory(dir);
    } catch (_) {}
    if (mounted) setState(() => _busy = false);
  }

  Future<void> _up() async {
    final ble = Provider.of<BleProvider>(context, listen: false);
    setState(() => _busy = true);
    try {
      await ble.navigateUp();
    } catch (_) {}
    if (mounted) setState(() => _busy = false);
  }

  String _fullPath(BleProvider ble, FileItem f) {
    if (f.path != null && f.path!.isNotEmpty) return f.path!;
    final base = ble.currentPath.endsWith('/')
        ? ble.currentPath
        : '${ble.currentPath}/';
    return '$base${f.name}';
  }

  bool _isSub(FileItem f) => f.isFile && f.name.toLowerCase().endsWith('.sub');

  @override
  Widget build(BuildContext context) {
    return Consumer<BleProvider>(
      builder: (context, ble, _) {
        final items = ble.fileList;
        final dirs = items.where((f) => f.isDirectory).toList();
        final subs = items.where(_isSub).toList();
        return SizedBox(
          height: MediaQuery.of(context).size.height * 0.75,
          child: Column(
            children: [
              Padding(
                padding: const EdgeInsets.fromLTRB(16, 4, 8, 4),
                child: Row(
                  children: [
                    const Icon(Icons.sd_card, color: AppColors.cyberPink),
                    const SizedBox(width: 8),
                    Expanded(
                      child: Text(ble.currentPath,
                          maxLines: 1,
                          overflow: TextOverflow.ellipsis,
                          style: const TextStyle(
                              color: AppColors.primaryText,
                              fontWeight: FontWeight.w600)),
                    ),
                    if (ble.currentPath != '/' && ble.currentPath.isNotEmpty)
                      TextButton.icon(
                        onPressed: _busy ? null : _up,
                        icon: const Icon(Icons.arrow_upward, size: 16),
                        label: const Text('Up'),
                      ),
                  ],
                ),
              ),
              const Divider(height: 1, color: AppColors.divider),
              Expanded(
                child: _busy
                    ? const Center(
                        child: CircularProgressIndicator(
                            color: AppColors.cyberPink))
                    : ListView(
                        children: [
                          for (final d in dirs)
                            ListTile(
                              dense: true,
                              leading: const Icon(Icons.folder,
                                  color: AppColors.goldAccent),
                              title: Text(d.name,
                                  style: const TextStyle(
                                      color: AppColors.primaryText)),
                              trailing: const Icon(Icons.chevron_right,
                                  color: AppColors.secondaryText),
                              onTap: () => _enter(d.name),
                            ),
                          for (final f in subs)
                            CheckboxListTile(
                              dense: true,
                              activeColor: AppColors.cyberPink,
                              value: _selected.contains(_fullPath(ble, f)),
                              onChanged: (v) {
                                final p = _fullPath(ble, f);
                                setState(() {
                                  if (v == true) {
                                    _selected.add(p);
                                  } else {
                                    _selected.remove(p);
                                  }
                                });
                              },
                              secondary: const Icon(Icons.graphic_eq,
                                  color: AppColors.cyanAccent),
                              title: Text(f.name,
                                  style: const TextStyle(
                                      color: AppColors.primaryText)),
                            ),
                          if (dirs.isEmpty && subs.isEmpty)
                            const Padding(
                              padding: EdgeInsets.all(28),
                              child: Center(
                                child: Text('No .sub files or folders here.',
                                    style: TextStyle(
                                        color: AppColors.secondaryText)),
                              ),
                            ),
                        ],
                      ),
              ),
              SafeArea(
                child: Padding(
                  padding: const EdgeInsets.all(12),
                  child: Row(
                    children: [
                      Text('${_selected.length} selected',
                          style: const TextStyle(
                              color: AppColors.secondaryText)),
                      const Spacer(),
                      ElevatedButton(
                        style: ElevatedButton.styleFrom(
                          backgroundColor: AppColors.cyberPink,
                          foregroundColor: Colors.white,
                        ),
                        onPressed: _selected.isEmpty
                            ? null
                            : () => Navigator.pop(
                                  context,
                                  _selected
                                      .map((p) => PlaylistItem.file(path: p))
                                      .toList(),
                                ),
                        child: const Text('Add selected'),
                      ),
                    ],
                  ),
                ),
              ),
            ],
          ),
        );
      },
    );
  }
}

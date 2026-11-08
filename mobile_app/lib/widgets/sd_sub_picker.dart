import 'package:flutter/material.dart';
import 'package:provider/provider.dart';

import '../models/file_item.dart';
import '../providers/ble_provider.dart';
import '../theme/app_colors.dart';
import 'gradient_widgets.dart';

/// Folder-by-folder browser of the .sub files on the Crow's SD card (live,
/// over BLE). Returns the picked SD-absolute paths. In [single] mode tapping a
/// file returns it straight away.
class SdSubPicker extends StatefulWidget {
  final bool single;
  final String title;
  const SdSubPicker(
      {super.key, this.single = false, this.title = 'Pick .sub files'});

  static Future<List<String>?> show(BuildContext context,
      {bool single = false, String title = 'Pick .sub files'}) {
    return showModalBottomSheet<List<String>>(
      context: context,
      backgroundColor: AppColors.primaryBackground,
      isScrollControlled: true,
      showDragHandle: false,
      shape: const RoundedRectangleBorder(
        borderRadius: BorderRadius.vertical(top: Radius.circular(20)),
      ),
      builder: (_) => SdSubPicker(single: single, title: title),
    );
  }

  @override
  State<SdSubPicker> createState() => _SdSubPickerState();
}

class _SdSubPickerState extends State<SdSubPicker> {
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
    final base =
        ble.currentPath.endsWith('/') ? ble.currentPath : '${ble.currentPath}/';
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
              GradientSheetHeader(
                title: widget.title,
                subtitle: widget.single
                    ? 'tap a file to load it'
                    : 'drag down to close',
              ),
              Padding(
                padding: const EdgeInsets.fromLTRB(16, 8, 8, 4),
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
                          if (widget.single)
                            for (final f in subs)
                              ListTile(
                                dense: true,
                                leading: const Icon(Icons.graphic_eq,
                                    color: AppColors.cyanAccent),
                                title: Text(f.name,
                                    style: const TextStyle(
                                        color: AppColors.primaryText)),
                                onTap: () =>
                                    Navigator.pop(context, [_fullPath(ble, f)]),
                              ),
                          if (!widget.single)
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
              if (!widget.single)
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
                                    _selected.toList(),
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

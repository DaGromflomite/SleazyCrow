import 'dart:async';
import 'package:flutter/material.dart';
import 'package:flutter_blue_plus/flutter_blue_plus.dart';
import 'package:provider/provider.dart';
import '../providers/ble_provider.dart';
import '../providers/detection_provider.dart';
import '../detection/detection_models.dart';
import '../theme/app_colors.dart';
import '../widgets/gradient_widgets.dart';
import 'hunt_screen.dart';

/// Phase 1 — BLE Scanner (the backbone of the phone-radio detection suite).
///
/// Runs purely on the phone's own Bluetooth — NO Evil Crow required. It lists
/// every nearby BLE advertiser with live signal strength. Later phases build on
/// this same scan stream: detection grading (trackers / Flock / Axon / cameras…)
/// in P2, and the "hotter/colder" fox hunt in its own Hunt sub-tab.
///
/// Kept deliberately self-contained for now; when P2 adds the signature engine
/// and P5 adds background alerts, the scan ownership moves into a DetectionProvider.
class DetectScreen extends StatefulWidget {
  const DetectScreen({super.key});

  @override
  State<DetectScreen> createState() => _DetectScreenState();
}

class _DetectScreenState extends State<DetectScreen> {
  /// Latest advert per device, keyed by address so the list is stable as RSSI updates.
  final Map<DeviceIdentifier, ScanResult> _devices = {};
  StreamSubscription<List<ScanResult>>? _resultsSub;
  StreamSubscription<bool>? _scanStateSub;
  bool _scanning = false;
  String? _error;

  @override
  void initState() {
    super.initState();
    _scanStateSub = FlutterBluePlus.isScanning.listen((s) {
      if (mounted) setState(() => _scanning = s);
    });
    // Auto-start the scan once the first frame is up.
    WidgetsBinding.instance.addPostFrameCallback((_) => _start());
  }

  Future<void> _start() async {
    if (!mounted) return;
    setState(() => _error = null);

    // Reuse the Crow app's proven "permissions settled + adapter on" check.
    final ble = context.read<BleProvider>();
    final ready = await ble.ensureBluetoothReady();
    if (!ready) {
      if (mounted) {
        setState(() => _error =
            ble.statusMessage.isEmpty ? 'Bluetooth not ready' : ble.statusMessage);
      }
      return;
    }

    try {
      // Only one BLE scan can run at a time; clear any existing one first
      // (e.g. the Connect tab's device scan) before starting ours.
      if (FlutterBluePlus.isScanningNow) {
        await FlutterBluePlus.stopScan();
      }
      _devices.clear();
      _resultsSub?.cancel();
      _resultsSub = FlutterBluePlus.scanResults.listen(
        (results) {
          for (final r in results) {
            _devices[r.device.remoteId] = r;
          }
          if (mounted) setState(() {});
        },
        onError: (e) {
          if (mounted) setState(() => _error = e.toString());
        },
      );
      // continuousUpdates keeps RSSI flowing live (needed for the hunt later).
      await FlutterBluePlus.startScan(
        continuousUpdates: true,
        androidScanMode: AndroidScanMode.lowLatency,
      );
    } catch (e) {
      if (mounted) setState(() => _error = 'Scan error: $e');
    }
  }

  Future<void> _stop() async {
    try {
      await FlutterBluePlus.stopScan();
    } catch (_) {/* ignore */}
  }

  Future<void> _toggle() async {
    if (_scanning) {
      await _stop();
    } else {
      await _start();
    }
  }

  @override
  void dispose() {
    _resultsSub?.cancel();
    _scanStateSub?.cancel();
    // Stop scanning when leaving the tab so we don't drain the battery or
    // block the Connect tab's Crow scan.
    FlutterBluePlus.stopScan();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final detector = context.watch<DetectionProvider>();

    // Evaluate each device against the enabled signatures.
    final list = _devices.values.toList();
    final detections = <DeviceIdentifier, Detection>{};
    for (final r in list) {
      final d = detector.evaluate(r);
      if (d != null) detections[r.device.remoteId] = d;
    }
    // Flagged devices float to the top; then by signal strength.
    list.sort((a, b) {
      final da = detections.containsKey(a.device.remoteId);
      final db = detections.containsKey(b.device.remoteId);
      if (da != db) return da ? -1 : 1;
      return b.rssi.compareTo(a.rssi);
    });

    return GradientBackground(
      child: Column(
        children: [
          _buildHeader(list.length, detections.length),
          if (_error != null) _buildError(_error!),
          Expanded(
            child: list.isEmpty ? _buildEmpty() : _buildList(list, detections),
          ),
        ],
      ),
    );
  }

  /// Open the fox hunt for a tapped device, then resume the list scan on return.
  Future<void> _huntDevice(ScanResult r) async {
    final name = r.device.platformName.isNotEmpty
        ? r.device.platformName
        : (r.advertisementData.advName.isNotEmpty
            ? r.advertisementData.advName
            : r.device.remoteId.str);
    await _stop(); // only one scan at a time; hand the radio to the hunt
    if (!mounted) return;
    await Navigator.of(context).push(
      MaterialPageRoute(
        builder: (_) => HuntScreen(targetId: r.device.remoteId, targetName: name),
      ),
    );
    if (mounted) _start(); // back on the list — resume scanning
  }

  // ---- UI pieces -----------------------------------------------------------

  Widget _buildHeader(int count, int flagged) {
    return Padding(
      padding: const EdgeInsets.fromLTRB(12, 10, 4, 6),
      child: Row(
        children: [
          Icon(Icons.radar,
              size: 18,
              color: _scanning ? AppColors.cyanAccent : AppColors.secondaryText),
          const SizedBox(width: 8),
          Expanded(
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              mainAxisSize: MainAxisSize.min,
              children: [
                Text(
                  _scanning ? 'Scanning · $count nearby' : '$count found',
                  style: const TextStyle(
                    color: AppColors.primaryText,
                    fontWeight: FontWeight.w600,
                  ),
                ),
                if (flagged > 0)
                  Text(
                    '$flagged flagged',
                    style: const TextStyle(
                      color: AppColors.error,
                      fontSize: 12,
                      fontWeight: FontWeight.w700,
                    ),
                  ),
              ],
            ),
          ),
          IconButton(
            tooltip: 'Detection filters',
            onPressed: _openFilters,
            icon: const Icon(Icons.tune, color: AppColors.secondaryText),
          ),
          GradientButton(
            onPressed: _toggle,
            icon: _scanning ? Icons.stop : Icons.play_arrow,
            label: _scanning ? 'Stop' : 'Scan',
            padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 9),
          ),
        ],
      ),
    );
  }

  /// Bottom sheet of per-category toggles (what to flag). iBeacon starts off.
  void _openFilters() {
    showModalBottomSheet<void>(
      context: context,
      backgroundColor: AppColors.secondaryBackground,
      showDragHandle: true,
      isScrollControlled: true,
      builder: (ctx) {
        return Consumer<DetectionProvider>(
          builder: (ctx, detector, _) {
            final cats = detector.categories;
            return SafeArea(
              child: Column(
                mainAxisSize: MainAxisSize.min,
                children: [
                  const Padding(
                    padding: EdgeInsets.fromLTRB(20, 4, 20, 8),
                    child: Align(
                      alignment: Alignment.centerLeft,
                      child: Text(
                        'What to flag',
                        style: TextStyle(
                          color: AppColors.primaryText,
                          fontSize: 18,
                          fontWeight: FontWeight.bold,
                        ),
                      ),
                    ),
                  ),
                  if (cats.isEmpty)
                    const Padding(
                      padding: EdgeInsets.all(20),
                      child: Text('Loading signatures…',
                          style: TextStyle(color: AppColors.secondaryText)),
                    ),
                  Flexible(
                    child: ListView(
                      shrinkWrap: true,
                      children: [
                        for (final c in cats)
                          SwitchListTile(
                            dense: true,
                            activeThumbColor: AppColors.cyberPink,
                            title: Text(c.label,
                                style: const TextStyle(
                                    color: AppColors.primaryText)),
                            subtitle: Text(c.severity,
                                style: const TextStyle(
                                    color: AppColors.secondaryText,
                                    fontSize: 12)),
                            value: detector.isEnabled(c.id),
                            onChanged: (v) => detector.setEnabled(c.id, v),
                          ),
                      ],
                    ),
                  ),
                  const SizedBox(height: 8),
                ],
              ),
            );
          },
        );
      },
    );
  }

  Widget _buildError(String msg) {
    return Container(
      width: double.infinity,
      margin: const EdgeInsets.symmetric(horizontal: 12),
      padding: const EdgeInsets.all(10),
      decoration: BoxDecoration(
        color: AppColors.error.withValues(alpha: 0.15),
        borderRadius: BorderRadius.circular(8),
        border: Border.all(color: AppColors.error),
      ),
      child: Row(
        children: [
          const Icon(Icons.warning_amber, color: AppColors.error, size: 18),
          const SizedBox(width: 8),
          Expanded(
            child: Text(msg,
                style: const TextStyle(color: AppColors.primaryText, fontSize: 13)),
          ),
        ],
      ),
    );
  }

  Widget _buildEmpty() {
    return Center(
      child: Column(
        mainAxisSize: MainAxisSize.min,
        children: [
          Icon(Icons.bluetooth_searching,
              size: 48, color: AppColors.secondaryText.withValues(alpha: 0.5)),
          const SizedBox(height: 12),
          Text(
            _scanning ? 'Listening for nearby devices…' : 'No devices yet',
            style: const TextStyle(color: AppColors.secondaryText),
          ),
          const SizedBox(height: 4),
          const Text(
            'Works with no Crow attached.',
            style: TextStyle(color: AppColors.disabledText, fontSize: 12),
          ),
        ],
      ),
    );
  }

  Widget _buildList(
      List<ScanResult> results, Map<DeviceIdentifier, Detection> detections) {
    return ListView.separated(
      padding: const EdgeInsets.fromLTRB(8, 4, 8, 12),
      itemCount: results.length,
      separatorBuilder: (_, __) =>
          const Divider(height: 1, color: AppColors.divider),
      itemBuilder: (_, i) =>
          _buildTile(results[i], detections[results[i].device.remoteId]),
    );
  }

  Widget _buildTile(ScanResult r, Detection? detection) {
    final name = r.device.platformName.isNotEmpty
        ? r.device.platformName
        : (r.advertisementData.advName.isNotEmpty
            ? r.advertisementData.advName
            : '(unknown)');
    final mfr = _manufacturerHint(r.advertisementData.manufacturerData);

    return ListTile(
      dense: true,
      onTap: () => _huntDevice(r),
      leading: _signalIcon(r.rssi),
      title: Text(
        name,
        maxLines: 1,
        overflow: TextOverflow.ellipsis,
        style: const TextStyle(
            color: AppColors.primaryText, fontWeight: FontWeight.w500),
      ),
      isThreeLine: detection != null,
      subtitle: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Text(
            '${r.device.remoteId.str}${mfr != null ? '  ·  $mfr' : ''}',
            maxLines: 1,
            overflow: TextOverflow.ellipsis,
            style: const TextStyle(color: AppColors.secondaryText, fontSize: 12),
          ),
          if (detection != null) ...[
            const SizedBox(height: 4),
            _detectionBadge(detection),
          ],
        ],
      ),
      trailing: Row(
        mainAxisSize: MainAxisSize.min,
        children: [
          Text(
            '${r.rssi} dBm',
            style: TextStyle(
              color: _rssiColor(r.rssi),
              fontWeight: FontWeight.w600,
              fontSize: 13,
            ),
          ),
          const SizedBox(width: 6),
          const Icon(Icons.my_location,
              size: 16, color: AppColors.cyberPurple),
        ],
      ),
    );
  }

  // ---- helpers -------------------------------------------------------------

  /// Rough company-ID hint — a tiny taste of what P2's signature engine will do.
  String? _manufacturerHint(Map<int, List<int>> mfrData) {
    if (mfrData.isEmpty) return null;
    final id = mfrData.keys.first;
    const known = {
      0x004C: 'Apple',
      0x0006: 'Microsoft',
      0x0075: 'Samsung',
      0x00E0: 'Google',
      0x0171: 'Amazon',
      0x02E5: 'Espressif',
    };
    final label = known[id];
    return label ?? 'Mfr 0x${id.toRadixString(16).padLeft(4, '0').toUpperCase()}';
  }

  Widget _detectionBadge(Detection d) {
    final color = _gradeColor(d.grade);
    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 3),
      decoration: BoxDecoration(
        color: color.withValues(alpha: 0.18),
        borderRadius: BorderRadius.circular(6),
        border: Border.all(color: color),
      ),
      child: Row(
        mainAxisSize: MainAxisSize.min,
        children: [
          Icon(Icons.warning_amber_rounded, size: 13, color: color),
          const SizedBox(width: 4),
          Text(
            '${d.category.label} · ${d.grade.label}',
            style: TextStyle(
                color: color, fontSize: 11, fontWeight: FontWeight.w700),
          ),
        ],
      ),
    );
  }

  Color _gradeColor(Grade g) => switch (g) {
        Grade.high => AppColors.error,
        Grade.med => AppColors.goldAccent,
        Grade.low => AppColors.secondaryText,
      };

  Color _rssiColor(int rssi) {
    if (rssi >= -60) return AppColors.success; // very close
    if (rssi >= -75) return AppColors.goldAccent; // nearby
    return AppColors.secondaryText; // far / weak
  }

  Widget _signalIcon(int rssi) {
    IconData icon;
    if (rssi >= -60) {
      icon = Icons.signal_cellular_alt;
    } else if (rssi >= -75) {
      icon = Icons.signal_cellular_alt_2_bar;
    } else {
      icon = Icons.signal_cellular_alt_1_bar;
    }
    return Icon(icon, color: _rssiColor(rssi), size: 22);
  }
}

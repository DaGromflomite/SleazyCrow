import 'dart:async';
import 'package:flutter/material.dart';
import 'package:flutter_blue_plus/flutter_blue_plus.dart';
import 'package:provider/provider.dart';
import '../providers/ble_provider.dart';
import '../providers/detection_provider.dart';
import '../detection/detection_models.dart';
import '../services/classic_bt.dart';
import '../theme/app_colors.dart';
import '../widgets/gradient_widgets.dart';
import '../widgets/logo_watermark.dart';
import 'hunt_screen.dart';

/// A row in the Detect list — either a BLE advert or a classic-BT device.
class _Row {
  final ScanResult? ble;
  final ClassicBtDevice? classic;
  final Detection? detection;
  _Row.ble(this.ble, this.detection) : classic = null;
  _Row.classic(this.classic, this.detection) : ble = null;
  bool get isClassic => classic != null;
  int get rssi => ble?.rssi ?? classic!.rssi;
}

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

  /// Classic Bluetooth (BR/EDR) devices from native inquiry, keyed by address.
  final Map<String, ClassicBtDevice> _classic = {};
  StreamSubscription<ClassicBtDevice?>? _classicSub;
  bool _classicSupported = false;

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
      // Classic Bluetooth inquiry runs alongside BLE (catches skimmer modules
      // and other BR/EDR gear BLE can't see).
      _startClassic();
    } catch (e) {
      if (mounted) setState(() => _error = 'Scan error: $e');
    }
  }

  /// Start classic-BT inquiry and keep it cycling while we're scanning.
  Future<void> _startClassic() async {
    _classicSupported = await ClassicBt.isSupported();
    if (!_classicSupported) return;
    _classicSub ??= ClassicBt.events().listen((d) {
      if (!mounted) return;
      if (d == null) {
        // one inquiry cycle finished — re-arm while still scanning
        if (_scanning) ClassicBt.startDiscovery();
        return;
      }
      _classic[d.address] = d;
      setState(() {});
    });
    await ClassicBt.startDiscovery();
  }

  Future<void> _stop() async {
    try {
      await FlutterBluePlus.stopScan();
    } catch (_) {/* ignore */}
    await ClassicBt.stopDiscovery();
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
    _classicSub?.cancel();
    // Stop scanning when leaving the tab so we don't drain the battery or
    // block the Connect tab's Crow scan.
    FlutterBluePlus.stopScan();
    ClassicBt.stopDiscovery();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final detector = context.watch<DetectionProvider>();

    // Merge BLE adverts + classic-BT devices, each evaluated against signatures.
    final rows = <_Row>[
      for (final r in _devices.values) _Row.ble(r, detector.evaluate(r)),
      for (final c in _classic.values)
        _Row.classic(c, detector.evaluateClassic(c.name)),
    ];
    final flagged = rows.where((e) => e.detection != null).length;
    // Flagged devices float to the top; then by signal strength.
    rows.sort((a, b) {
      final fa = a.detection != null, fb = b.detection != null;
      if (fa != fb) return fa ? -1 : 1;
      return b.rssi.compareTo(a.rssi);
    });

    return GradientBackground(
      child: Column(
        children: [
          _buildHeader(rows.length, flagged),
          if (_error != null) _buildError(_error!),
          Expanded(
            child: rows.isEmpty ? _buildEmpty() : _buildList(rows),
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
      showDragHandle: false,
      isScrollControlled: true,
      shape: const RoundedRectangleBorder(
        borderRadius: BorderRadius.vertical(top: Radius.circular(20)),
      ),
      builder: (ctx) {
        return Consumer<DetectionProvider>(
          builder: (ctx, detector, _) {
            final cats = detector.categories;
            return SafeArea(
              top: false,
              child: Column(
                mainAxisSize: MainAxisSize.min,
                children: [
                  const GradientSheetHeader(
                    title: 'What to flag',
                    subtitle: 'drag down to close',
                  ),
                  const SizedBox(height: 4),
                  if (cats.isEmpty)
                    const Padding(
                      padding: EdgeInsets.all(20),
                      child: Text('Loading signatures…',
                          style: TextStyle(color: AppColors.secondaryText)),
                    ),
                  Flexible(
                    child: ListView(
                      shrinkWrap: true,
                      children: _filterRows(cats, detector),
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
      child: Stack(
        alignment: Alignment.center,
        children: [
          const LogoWatermark(size: 300, opacity: 0.06),
          Column(
            mainAxisSize: MainAxisSize.min,
            children: [
              Icon(Icons.bluetooth_searching,
                  size: 44,
                  color: AppColors.secondaryText.withValues(alpha: 0.5)),
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
        ],
      ),
    );
  }

  Widget _buildList(List<_Row> rows) {
    return ListView.separated(
      padding: const EdgeInsets.fromLTRB(8, 4, 8, 12),
      itemCount: rows.length,
      separatorBuilder: (_, __) =>
          const Divider(height: 1, color: AppColors.divider),
      itemBuilder: (_, i) => rows[i].isClassic
          ? _buildClassicTile(rows[i].classic!, rows[i].detection)
          : _buildTile(rows[i].ble!, rows[i].detection),
    );
  }

  Widget _buildClassicTile(ClassicBtDevice d, Detection? detection) {
    final name = (d.name != null && d.name!.isNotEmpty) ? d.name! : '(unknown)';
    final hasRssi = d.rssi > -127;
    return ListTile(
      dense: true,
      leading: hasRssi
          ? _signalIcon(d.rssi)
          : const Icon(Icons.bluetooth,
              color: AppColors.secondaryText, size: 22),
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
            '${d.address}  ·  Classic BT',
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
      trailing: Text(
        hasRssi ? '${d.rssi} dBm' : '—',
        style: TextStyle(
          color: hasRssi ? _rssiColor(d.rssi) : AppColors.secondaryText,
          fontWeight: FontWeight.w600,
          fontSize: 13,
        ),
      ),
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
          Flexible(
            child: Text(
              '${d.category.label} · ${d.grade.label}',
              maxLines: 1,
              overflow: TextOverflow.ellipsis,
              softWrap: false,
              style: TextStyle(
                  color: color, fontSize: 11, fontWeight: FontWeight.w700),
            ),
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

  /// Friendly, human-readable label for a category's severity group, shown as
  /// the subtitle in the "What to flag" sheet instead of the raw keyword.
  static String _severityLabel(String severity) => switch (severity) {
        'surveillance' => 'Surveillance',
        'tracker' => 'Item tracker',
        'tool' => 'Hacker tool',
        'skimmer' => 'Card skimmer',
        'drone' => 'Drone / Remote ID',
        'info' => 'Other',
        _ => severity,
      };

  /// Section a severity belongs to in the "What to flag" sheet: (order, title).
  /// Several severities collapse into one section (skimmer joins Tools).
  static (int, String) _section(String severity) => switch (severity) {
        'surveillance' => (0, 'Surveillance'),
        'tracker' => (1, 'Trackers'),
        'drone' => (2, 'Drones'),
        'tool' => (3, 'Tools'),
        'skimmer' => (3, 'Tools'),
        _ => (4, 'Other'),
      };

  /// Build the toggle rows grouped under section headers, preserving the DB
  /// order within each section.
  List<Widget> _filterRows(
      List<DetectionCategory> cats, DetectionProvider detector) {
    final rows = <Widget>[];
    final seen = <String>{};
    // Stable section order by first appearance / defined order.
    final order = cats.map((c) => _section(c.severity)).toList()
      ..sort((a, b) => a.$1.compareTo(b.$1));
    final sections = <String>[];
    for (final s in order) {
      if (seen.add(s.$2)) sections.add(s.$2);
    }
    for (final title in sections) {
      rows.add(Padding(
        padding: const EdgeInsets.fromLTRB(20, 14, 20, 4),
        child: Text(
          title.toUpperCase(),
          style: const TextStyle(
            color: AppColors.cyberPink,
            fontSize: 12,
            fontWeight: FontWeight.w700,
            letterSpacing: 1.1,
          ),
        ),
      ));
      for (final c in cats.where((c) => _section(c.severity).$2 == title)) {
        rows.add(SwitchListTile(
          dense: true,
          activeThumbColor: AppColors.cyberPink,
          title: Text(c.label,
              style: const TextStyle(color: AppColors.primaryText)),
          subtitle: Text(_severityLabel(c.severity),
              style: const TextStyle(
                  color: AppColors.secondaryText, fontSize: 12)),
          value: detector.isEnabled(c.id),
          onChanged: (v) => detector.setEnabled(c.id, v),
        ));
      }
    }
    return rows;
  }

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

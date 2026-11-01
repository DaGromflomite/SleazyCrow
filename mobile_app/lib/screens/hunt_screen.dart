import 'dart:async';
import 'dart:math' as math;
import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:flutter_blue_plus/flutter_blue_plus.dart';
import 'package:provider/provider.dart';
import '../providers/ble_provider.dart';
import '../theme/app_colors.dart';
import '../widgets/gradient_widgets.dart';

/// Phase 1 — BLE fox hunt. Pick a device in the Detect list, then walk toward it
/// using a "hotter/colder" signal meter plus haptic pulses that speed up as you
/// close in. Phones can't do true compass bearing (no BT5.1 antenna array), so
/// this is smoothed RSSI trend — which works on every phone.
class HuntScreen extends StatefulWidget {
  final DeviceIdentifier targetId;
  final String targetName;

  const HuntScreen({
    super.key,
    required this.targetId,
    required this.targetName,
  });

  @override
  State<HuntScreen> createState() => _HuntScreenState();
}

class _HuntScreenState extends State<HuntScreen> {
  StreamSubscription<List<ScanResult>>? _resultsSub;
  Timer? _pulseTimer;

  double? _smoothedRssi; // exponential moving average
  int? _rawRssi;
  double? _baselineRssi; // for the trend arrow
  DateTime _lastSeen = DateTime.fromMillisecondsSinceEpoch(0);
  bool _hunting = false;
  String? _error;

  // RSSI window we map to 0..100% (−95 dBm = far, −40 dBm = right on top of it).
  static const double _rssiFloor = -95;
  static const double _rssiCeil = -40;

  @override
  void initState() {
    super.initState();
    WidgetsBinding.instance.addPostFrameCallback((_) => _start());
  }

  Future<void> _start() async {
    if (!mounted) return;
    setState(() => _error = null);

    final ble = context.read<BleProvider>();
    if (!await ble.ensureBluetoothReady()) {
      if (mounted) {
        setState(() => _error =
            ble.statusMessage.isEmpty ? 'Bluetooth not ready' : ble.statusMessage);
      }
      return;
    }

    try {
      if (FlutterBluePlus.isScanningNow) {
        await FlutterBluePlus.stopScan();
      }
      _resultsSub?.cancel();
      _resultsSub = FlutterBluePlus.scanResults.listen((results) {
        for (final r in results) {
          if (r.device.remoteId == widget.targetId) {
            _ingest(r.rssi);
            break;
          }
        }
      }, onError: (e) {
        if (mounted) setState(() => _error = e.toString());
      });

      await FlutterBluePlus.startScan(
        continuousUpdates: true,
        androidScanMode: AndroidScanMode.lowLatency,
        withRemoteIds: [widget.targetId.str], // focus on just our target
      );
      setState(() => _hunting = true);
      _startPulsing();
    } catch (e) {
      if (mounted) setState(() => _error = 'Hunt error: $e');
    }
  }

  void _ingest(int rssi) {
    _rawRssi = rssi;
    _lastSeen = DateTime.now();
    // Exponential moving average to tame the jitter.
    _smoothedRssi =
        _smoothedRssi == null ? rssi.toDouble() : _smoothedRssi! * 0.7 + rssi * 0.3;
    if (mounted) setState(() {});
  }

  /// Haptic pulse loop; the stronger the signal, the shorter the gap.
  void _startPulsing() {
    _pulseTimer?.cancel();
    void schedule() {
      final pct = _strength();
      // 900ms when far → 90ms when right on top of it.
      final gap = (900 - pct * 810).clamp(90, 900).toInt();
      _pulseTimer = Timer(Duration(milliseconds: gap), () {
        if (!mounted || !_hunting) return;
        if (_isLive()) HapticFeedback.lightImpact();
        schedule();
      });
    }

    schedule();
  }

  bool _isLive() =>
      DateTime.now().difference(_lastSeen) < const Duration(seconds: 4);

  /// 0..1 normalized signal strength from the smoothed RSSI.
  double _strength() {
    if (_smoothedRssi == null) return 0;
    return ((_smoothedRssi! - _rssiFloor) / (_rssiCeil - _rssiFloor))
        .clamp(0.0, 1.0);
  }

  @override
  void dispose() {
    _hunting = false;
    _resultsSub?.cancel();
    _pulseTimer?.cancel();
    FlutterBluePlus.stopScan();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final live = _isLive() && _smoothedRssi != null;
    final pct = _strength();

    return Scaffold(
      appBar: AppBar(
        backgroundColor: Colors.transparent,
        title: Text(widget.targetName,
            maxLines: 1, overflow: TextOverflow.ellipsis),
      ),
      extendBodyBehindAppBar: true,
      body: GradientBackground(
        child: SafeArea(
          child: Padding(
            padding: const EdgeInsets.all(20),
            child: Column(
              children: [
              Text(
                widget.targetId.str,
                style: const TextStyle(
                    color: AppColors.secondaryText, fontSize: 12),
              ),
              const SizedBox(height: 8),
              if (_error != null) _errorText(_error!),
              Expanded(child: Center(child: _meter(pct, live))),
              _trendLabel(live, pct),
              const SizedBox(height: 24),
              _stopButton(),
              ],
            ),
          ),
        ),
      ),
    );
  }

  Widget _meter(double pct, bool live) {
    final color = _strengthColor(pct);
    return SizedBox(
      width: 240,
      height: 240,
      child: Stack(
        alignment: Alignment.center,
        children: [
          CustomPaint(
            size: const Size(240, 240),
            painter: _RingPainter(live ? pct : 0),
          ),
          Column(
            mainAxisSize: MainAxisSize.min,
            children: [
              Text(
                live ? '${(pct * 100).round()}%' : '—',
                style: TextStyle(
                  color: live ? color : AppColors.secondaryText,
                  fontSize: 52,
                  fontWeight: FontWeight.bold,
                ),
              ),
              Text(
                live && _rawRssi != null ? '$_rawRssi dBm' : 'searching…',
                style: const TextStyle(
                    color: AppColors.secondaryText, fontSize: 14),
              ),
            ],
          ),
        ],
      ),
    );
  }

  Widget _trendLabel(bool live, double pct) {
    if (!live) {
      return const Text('Signal lost — move around to pick it back up',
          textAlign: TextAlign.center,
          style: TextStyle(color: AppColors.secondaryText));
    }
    // Compare smoothed value against a slow baseline for a closer/farther read.
    final s = _smoothedRssi!;
    _baselineRssi ??= s;
    final delta = s - _baselineRssi!;
    _baselineRssi = _baselineRssi! * 0.9 + s * 0.1; // drift the baseline slowly

    String text;
    IconData icon;
    Color color;
    if (delta > 1.5) {
      text = 'Getting WARMER';
      icon = Icons.arrow_upward;
      color = AppColors.success;
    } else if (delta < -1.5) {
      text = 'Getting colder';
      icon = Icons.arrow_downward;
      color = AppColors.secondaryText;
    } else {
      text = 'Hold steady…';
      icon = Icons.remove;
      color = AppColors.goldAccent;
    }
    return Row(
      mainAxisAlignment: MainAxisAlignment.center,
      children: [
        Icon(icon, color: color),
        const SizedBox(width: 8),
        Text(text,
            style: TextStyle(
                color: color, fontSize: 18, fontWeight: FontWeight.w600)),
      ],
    );
  }

  Widget _stopButton() {
    return GradientButton(
      expand: true,
      icon: Icons.stop,
      label: 'Stop hunt',
      padding: const EdgeInsets.symmetric(vertical: 14),
      onPressed: () => Navigator.of(context).pop(),
    );
  }

  Widget _errorText(String msg) => Padding(
        padding: const EdgeInsets.only(bottom: 8),
        child: Text(msg,
            textAlign: TextAlign.center,
            style: const TextStyle(color: AppColors.error, fontSize: 13)),
      );

  Color _strengthColor(double pct) {
    if (pct >= 0.66) return AppColors.success;
    if (pct >= 0.33) return AppColors.goldAccent;
    return AppColors.cyanAccent;
  }
}

/// Circular signal meter drawn with the cyberpunk pink->purple sweep gradient.
class _RingPainter extends CustomPainter {
  final double value; // 0..1
  _RingPainter(this.value);

  @override
  void paint(Canvas canvas, Size size) {
    const stroke = 16.0;
    final center = Offset(size.width / 2, size.height / 2);
    final radius = (size.shortestSide - stroke) / 2;
    final rect = Rect.fromCircle(center: center, radius: radius);

    // Background track
    final bg = Paint()
      ..style = PaintingStyle.stroke
      ..strokeWidth = stroke
      ..strokeCap = StrokeCap.round
      ..color = AppColors.surfaceElevated;
    canvas.drawCircle(center, radius, bg);

    if (value <= 0) return;

    // Foreground arc with the pink->purple sweep.
    final fg = Paint()
      ..style = PaintingStyle.stroke
      ..strokeWidth = stroke
      ..strokeCap = StrokeCap.round
      ..shader = const SweepGradient(
        colors: [AppColors.cyberPink, AppColors.cyberPurple, AppColors.cyberPink],
        transform: GradientRotation(-math.pi / 2),
      ).createShader(rect);
    final sweep = value.clamp(0.0, 1.0) * 2 * math.pi;
    canvas.drawArc(rect, -math.pi / 2, sweep, false, fg);
  }

  @override
  bool shouldRepaint(_RingPainter old) => old.value != value;
}

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

  final List<int> _recent = []; // last few raw samples for a median
  double? _smoothedRssi; // median -> exponential moving average
  int? _rawRssi;
  double? _minRssi; // auto-ranged window, far end (weakest you've seen)
  double? _maxRssi; // auto-ranged window, near end (strongest you've seen)
  double? _trendRef; // slow lagging reference for warmer/colder
  DateTime _lastSeen = DateTime.fromMillisecondsSinceEpoch(0);
  bool _hunting = false;
  String? _error;

  // Never map a window narrower than this (dBm) — keeps a near-flat signal from
  // slamming the meter to 0 or 100 on pure noise.
  static const double _minSpan = 10;

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

    // 1) Median of the last few raw samples — kills the ±10 dBm spikes that
    //    make raw RSSI useless for hunting.
    _recent.add(rssi);
    if (_recent.length > 5) _recent.removeAt(0);
    final sorted = [..._recent]..sort();
    final median = sorted[sorted.length ~/ 2].toDouble();

    // 2) EMA to smooth what's left.
    _smoothedRssi =
        _smoothedRssi == null ? median : _smoothedRssi! * 0.7 + median * 0.3;
    final s = _smoothedRssi!;

    // 3) Auto-range the meter to the near/far extremes actually seen this hunt,
    //    relaxing slowly so a stale extreme doesn't pin the scale forever. This
    //    is what makes walking toward the target visibly fill the ring even when
    //    the whole room sits in a narrow dBm band.
    _maxRssi = _maxRssi == null ? s : math.max(s, _maxRssi! - 0.05);
    _minRssi = _minRssi == null ? s : math.min(s, _minRssi! + 0.05);

    // 4) Slow lagging reference (~ a few seconds) for the warmer/colder read.
    _trendRef = _trendRef == null ? s : _trendRef! * 0.95 + s * 0.05;

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

  /// 0..1 strength, mapped across the auto-ranged window (far..near) so the
  /// meter reflects how close you are *relative to this hunt*, not a fixed scale.
  double _strength() {
    final s = _smoothedRssi;
    if (s == null || _minRssi == null || _maxRssi == null) return 0;
    var lo = _minRssi!, hi = _maxRssi!;
    if (hi - lo < _minSpan) {
      final mid = (hi + lo) / 2;
      lo = mid - _minSpan / 2;
      hi = mid + _minSpan / 2;
    }
    return ((s - lo) / (hi - lo)).clamp(0.0, 1.0);
  }

  /// True once we've seen enough spread to trust the auto-ranged meter.
  bool get _calibrated =>
      _minRssi != null && _maxRssi != null && (_maxRssi! - _minRssi!) >= _minSpan;

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
    // Compare the smoothed value against the slow lagging reference (updated in
    // _ingest). ±2 dBm band gives hysteresis so it doesn't flicker on noise.
    final s = _smoothedRssi!;
    final delta = s - (_trendRef ?? s);

    String text;
    IconData icon;
    Color color;
    if (!_calibrated) {
      text = 'Calibrating — move closer, then farther';
      icon = Icons.open_in_full;
      color = AppColors.cyanAccent;
    } else if (delta > 2) {
      text = 'Getting WARMER';
      icon = Icons.arrow_upward;
      color = AppColors.success;
    } else if (delta < -2) {
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

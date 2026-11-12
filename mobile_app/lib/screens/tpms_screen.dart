import 'package:flutter/material.dart';
import 'package:provider/provider.dart';
import '../providers/ble_provider.dart';
import '../providers/tpms_provider.dart';
import '../services/tpms_protocols.dart';
import '../theme/app_colors.dart';

/// TPMS stalking-detection screen. The Crow decodes nearby cars' tire-pressure
/// sensor IDs; a sensor seen across several trips is flagged as a possible
/// follower. Needs the Crow connected and, to catch anything, a moving vehicle.
class TpmsScreen extends StatelessWidget {
  const TpmsScreen({super.key});

  @override
  Widget build(BuildContext context) {
    final ble = context.watch<BleProvider>();
    final tpms = context.watch<TpmsProvider>();
    final connected = ble.isConnected;
    final followers = tpms.liveSensors.where((s) => tpms.isFollower(s.id)).toList();

    return Scaffold(
      backgroundColor: AppColors.primaryBackground,
      body: SafeArea(
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.stretch,
          children: [
            _header(context, tpms, connected),
            if (followers.isNotEmpty) _followerBanner(followers.length),
            if (!connected) _notice(
                'Connect to the Crow first — TPMS reception uses its CC1101 radio.'),
            if (connected && tpms.scanning && tpms.packets == 0)
              _notice(
                  'Listening on ${tpms.freqMHz.toStringAsFixed(2)} MHz. TPMS mostly '
                  'transmits from MOVING cars — packets appear when a vehicle is '
                  'nearby and rolling.'),
            Expanded(
              child: tpms.liveSensors.isEmpty
                  ? _empty(tpms)
                  : ListView.builder(
                      itemCount: tpms.liveSensors.length,
                      itemBuilder: (_, i) =>
                          _sensorTile(tpms, tpms.liveSensors[i]),
                    ),
            ),
          ],
        ),
      ),
    );
  }

  Widget _header(BuildContext context, TpmsProvider tpms, bool connected) {
    return Container(
      padding: const EdgeInsets.fromLTRB(16, 12, 12, 12),
      decoration: const BoxDecoration(gradient: AppColors.cyberGradient),
      child: Row(
        children: [
          const Icon(Icons.radar, color: Colors.white, size: 22),
          const SizedBox(width: 10),
          Expanded(
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              mainAxisSize: MainAxisSize.min,
              children: [
                const Text('TPMS · follower detection',
                    style: TextStyle(
                        color: Colors.white,
                        fontWeight: FontWeight.w700,
                        fontSize: 15)),
                Text(
                  tpms.scanning
                      ? 'Scanning ${tpms.freqMHz.toStringAsFixed(2)} MHz · '
                          '${tpms.packets} packets · ${tpms.liveSensors.length} sensors'
                      : 'Idle · ${tpms.knownFollowers.length} known followers',
                  style: const TextStyle(color: Colors.white70, fontSize: 12),
                ),
              ],
            ),
          ),
          PopupMenuButton<String>(
            icon: const Icon(Icons.more_vert, color: Colors.white),
            onSelected: (v) {
              if (v == 'clear') _confirmClear(context, tpms);
              if (v == 'selftest') context.read<BleProvider>().runTpmsSelfTest();
            },
            itemBuilder: (_) => const [
              PopupMenuItem(
                  value: 'selftest',
                  child: Text('Run self-test (demo sensor)')),
              PopupMenuItem(value: 'clear', child: Text('Clear history')),
            ],
          ),
          FilledButton(
            onPressed: !connected
                ? null
                : () => tpms.scanning ? tpms.stop() : tpms.start(),
            style: FilledButton.styleFrom(
              backgroundColor:
                  tpms.scanning ? AppColors.error : Colors.white,
              foregroundColor:
                  tpms.scanning ? Colors.white : AppColors.cyberPink,
              padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 10),
            ),
            child: Text(tpms.scanning ? 'Stop' : 'Scan'),
          ),
        ],
      ),
    );
  }

  Widget _followerBanner(int n) => Container(
        width: double.infinity,
        color: AppColors.error.withValues(alpha: 0.15),
        padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 10),
        child: Row(children: [
          const Icon(Icons.warning_amber, color: AppColors.error, size: 18),
          const SizedBox(width: 8),
          Expanded(
            child: Text(
              '$n vehicle${n == 1 ? '' : 's'} seen this session '
              'already appeared on several past trips — possible follower${n == 1 ? '' : 's'}.',
              style: const TextStyle(
                  color: AppColors.error,
                  fontSize: 12,
                  fontWeight: FontWeight.w600),
            ),
          ),
        ]),
      );

  Widget _notice(String text) => Container(
        width: double.infinity,
        color: AppColors.secondaryBackground,
        padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 10),
        child: Text(text,
            style: const TextStyle(
                color: AppColors.secondaryText, fontSize: 12)),
      );

  Widget _empty(TpmsProvider tpms) => Center(
        child: Padding(
          padding: const EdgeInsets.all(28),
          child: Column(
            mainAxisSize: MainAxisSize.min,
            children: [
              Icon(Icons.directions_car_filled,
                  size: 48, color: AppColors.secondaryText.withValues(alpha: 0.5)),
              const SizedBox(height: 14),
              Text(
                tpms.scanning
                    ? 'No sensors yet. Drive, or wait for a moving car nearby.'
                    : 'Press Scan to start listening for nearby tire-pressure sensors.',
                textAlign: TextAlign.center,
                style:
                    const TextStyle(color: AppColors.secondaryText, fontSize: 13),
              ),
            ],
          ),
        ),
      );

  Widget _sensorTile(TpmsProvider tpms, TpmsSensor s) {
    final follower = tpms.isFollower(s.id);
    final trips = tpms.tripsFor(s.id);
    return Container(
      margin: const EdgeInsets.fromLTRB(12, 6, 12, 0),
      padding: const EdgeInsets.all(12),
      decoration: BoxDecoration(
        color: AppColors.secondaryBackground,
        borderRadius: BorderRadius.circular(10),
        border: Border.all(
            color: follower ? AppColors.error : AppColors.borderDefault,
            width: follower ? 1.5 : 1),
      ),
      child: Row(
        children: [
          Icon(follower ? Icons.gpp_maybe : Icons.sensors,
              color: follower ? AppColors.error : AppColors.cyanAccent,
              size: 22),
          const SizedBox(width: 12),
          Expanded(
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Row(
                  children: [
                    Text(s.hex,
                        style: const TextStyle(
                            color: AppColors.primaryText,
                            fontWeight: FontWeight.w700,
                            fontFamily: 'monospace',
                            fontSize: 14)),
                    const SizedBox(width: 8),
                    if (s.protocol != 0)
                      Container(
                        padding: const EdgeInsets.symmetric(
                            horizontal: 6, vertical: 1),
                        decoration: BoxDecoration(
                          color: AppColors.cyanAccent.withValues(alpha: 0.15),
                          borderRadius: BorderRadius.circular(4),
                        ),
                        child: Text(tpmsInfo(s.protocol).name,
                            style: const TextStyle(
                                color: AppColors.cyanAccent,
                                fontSize: 10,
                                fontWeight: FontWeight.w700)),
                      ),
                  ],
                ),
                if (s.protocol != 0) ...[
                  const SizedBox(height: 2),
                  Text(tpmsInfo(s.protocol).makes,
                      maxLines: 1,
                      overflow: TextOverflow.ellipsis,
                      style: const TextStyle(
                          color: AppColors.secondaryText, fontSize: 11)),
                ],
                const SizedBox(height: 2),
                Text(
                  [
                    'seen in $trips trip${trips == 1 ? '' : 's'}',
                    if (s.pressureKpa != null)
                      '${kpaToPsi(s.pressureKpa!).toStringAsFixed(1)} psi',
                    if (s.temperatureC != null) '${s.temperatureC}°C',
                  ].join(' · '),
                  style: const TextStyle(
                      color: AppColors.secondaryText, fontSize: 11),
                ),
              ],
            ),
          ),
          if (follower)
            Container(
              padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 3),
              decoration: BoxDecoration(
                color: AppColors.error.withValues(alpha: 0.18),
                borderRadius: BorderRadius.circular(6),
              ),
              child: const Text('FOLLOWER',
                  style: TextStyle(
                      color: AppColors.error,
                      fontSize: 10,
                      fontWeight: FontWeight.w800)),
            ),
        ],
      ),
    );
  }

  void _confirmClear(BuildContext context, TpmsProvider tpms) {
    showDialog(
      context: context,
      builder: (ctx) => AlertDialog(
        backgroundColor: AppColors.secondaryBackground,
        title: const Text('Clear TPMS history?',
            style: TextStyle(color: AppColors.primaryText)),
        content: const Text(
            'Forgets every remembered sensor and trip, so past followers no '
            'longer flag. This cannot be undone.',
            style: TextStyle(color: AppColors.secondaryText)),
        actions: [
          TextButton(
              onPressed: () => Navigator.pop(ctx),
              child: const Text('Cancel')),
          TextButton(
            onPressed: () {
              tpms.clearHistory();
              Navigator.pop(ctx);
            },
            child:
                const Text('Clear', style: TextStyle(color: AppColors.error)),
          ),
        ],
      ),
    );
  }
}

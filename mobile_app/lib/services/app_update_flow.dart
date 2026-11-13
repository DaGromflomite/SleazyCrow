// In-app self-update for the Sleazy Crow Android app.
//
// Source of truth: GitHub releases of DaGromflomite/SleazyCrow (see
// UpdateService.githubOwner/githubRepo) — never the upstream Senape repo.
// Flow: check -> changelog dialog -> download into app cache with MD5 check
// against the release's .apk.md5 -> "Install unknown apps" permission if
// needed -> system installer via the sleazycrow/installer platform channel
// (MainActivity.kt + FileProvider). Android only accepts the update if it is
// signed with the same Sleazy Crow key as the installed app.
import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:package_info_plus/package_info_plus.dart';
import 'package:shared_preferences/shared_preferences.dart';

import '../l10n/app_localizations.dart';
import '../theme/app_colors.dart';
import 'update_service.dart';

const MethodChannel _installer = MethodChannel('sleazycrow/installer');

const _kLastAutoCheck = 'app_update_last_auto_check_ms';
const _kAutoCheckInterval = Duration(hours: 12);

/// Quiet launch-time check: at most every 12 h, no UI unless an update exists.
Future<void> maybeAutoCheckAppUpdate(BuildContext context) async {
  try {
    final prefs = await SharedPreferences.getInstance();
    final now = DateTime.now().millisecondsSinceEpoch;
    final last = prefs.getInt(_kLastAutoCheck) ?? 0;
    if (now - last < _kAutoCheckInterval.inMilliseconds) return;
    await prefs.setInt(_kLastAutoCheck, now);
  } catch (_) {
    return;
  }
  if (!context.mounted) return;
  await checkAppUpdateFlow(context, silent: true);
}

/// Check for app updates on GitHub and show dialog
Future<void> checkAppUpdateFlow(BuildContext context, {bool silent = false}) async {
  try {
    final info = await PackageInfo.fromPlatform();
    final currentVersion = info.version;

    // Show loading
    if (context.mounted && !silent) {
      ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(
          content: Row(
            children: [
              const SizedBox(
                width: 16, height: 16,
                child: CircularProgressIndicator(strokeWidth: 2, color: Colors.white),
              ),
              const SizedBox(width: 12),
              Text(AppLocalizations.of(context)!.checkingForAppUpdates),
            ],
          ),
          duration: const Duration(seconds: 2),
        ),
      );
    }

    final update = await UpdateService.checkAppUpdate(currentVersion);

    if (!context.mounted) return;
    ScaffoldMessenger.of(context).hideCurrentSnackBar();

    if (update == null) {
      if (silent) return;
      ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(
          content: Text(AppLocalizations.of(context)!.appUpToDate(currentVersion)),
          backgroundColor: AppColors.success,
        ),
      );
      return;
    }

    // Show update available dialog
    showDialog(
      context: context,
      builder: (ctx) => AlertDialog(
        title: Row(
          children: [
            const Icon(Icons.system_update_alt, color: AppColors.primaryAccent),
            const SizedBox(width: 8),
            Text(AppLocalizations.of(context)!.appUpdateAvailable,
                style: const TextStyle(color: AppColors.primaryText, fontSize: 16)),
          ],
        ),
        content: Column(
          mainAxisSize: MainAxisSize.min,
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Text(AppLocalizations.of(context)!.currentVersionLabel(currentVersion),
                style: const TextStyle(color: AppColors.secondaryText, fontSize: 13)),
            Text(AppLocalizations.of(context)!.latestVersionLabel(update.version),
                style: const TextStyle(color: AppColors.success, fontSize: 16, fontWeight: FontWeight.bold)),
            const SizedBox(height: 12),
            Text(AppLocalizations.of(context)!.changelogLabel, style: const TextStyle(color: AppColors.primaryText, fontWeight: FontWeight.w600, fontSize: 13)),
            const SizedBox(height: 4),
            Container(
              constraints: const BoxConstraints(maxHeight: 150),
              child: SingleChildScrollView(
                child: Text(update.changelog,
                    style: const TextStyle(color: AppColors.secondaryText, fontSize: 12)),
              ),
            ),
          ],
        ),
        actions: [
          TextButton(
            onPressed: () => Navigator.of(ctx).pop(),
            child: Text(AppLocalizations.of(context)!.later),
          ),
          if (update.apkUrl != null)
            ElevatedButton.icon(
              onPressed: () async {
                Navigator.of(ctx).pop();
                downloadAndInstallApk(context, update);
              },
              icon: const Icon(Icons.download, size: 16),
              label: Text(AppLocalizations.of(context)!.downloadAndInstall),
              style: ElevatedButton.styleFrom(
                backgroundColor: AppColors.primaryAccent,
                foregroundColor: AppColors.primaryBackground,
              ),
            ),
        ],
      ),
    );
  } on UpdateServiceException catch (e) {
    if (context.mounted && !silent) {
      ScaffoldMessenger.of(context).hideCurrentSnackBar();
      ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(content: Text(AppLocalizations.of(context)!.updateCheckFailed(e.message)),
            backgroundColor: AppColors.error),
      );
    }
  } catch (e) {
    if (context.mounted && !silent) {
      ScaffoldMessenger.of(context).hideCurrentSnackBar();
      ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(content: Text('Error: $e'), backgroundColor: AppColors.error),
      );
    }
  }
}


/// Download the release APK (from DaGromflomite/SleazyCrow), verify its MD5
/// against the release's .md5 asset, then hand it to the Android installer.
/// The user always confirms in the system dialog; Android only accepts it if
/// it is signed with the same Sleazy Crow key as the installed app.
Future<void> downloadAndInstallApk(BuildContext context, AppUpdate update) async {
  if (update.apkUrl == null) return;
  final progress = ValueNotifier<double>(0);
  bool dialogOpen = true;
  showDialog(
    context: context,
    barrierDismissible: false,
    builder: (ctx) => AlertDialog(
      title: Text('Downloading v${update.version}',
          style: const TextStyle(color: AppColors.primaryText, fontSize: 16)),
      content: ValueListenableBuilder<double>(
        valueListenable: progress,
        builder: (_, p, __) => Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            LinearProgressIndicator(value: p > 0 ? p : null),
            const SizedBox(height: 8),
            Text('${(p * 100).toStringAsFixed(0)}%',
                style: const TextStyle(color: AppColors.secondaryText, fontSize: 12)),
          ],
        ),
      ),
    ),
  ).then((_) => dialogOpen = false);
  void closeDialog() {
    if (dialogOpen && context.mounted) Navigator.of(context, rootNavigator: true).pop();
  }

  try {
    String? md5Hash;
    if (update.md5Url != null) {
      md5Hash = await UpdateService.downloadMd5(update.md5Url!);
    }
    final apkPath = await UpdateService.downloadApk(
      update.apkUrl!,
      expectedMd5: md5Hash,
      onProgress: (p) => progress.value = p,
    );
    closeDialog();
    if (!context.mounted) return;

    // Android 8+: the user must allow "Install unknown apps" for Sleazy Crow once.
    final allowed = await _installer.invokeMethod<bool>('canInstall') ?? false;
    if (!allowed) {
      if (!context.mounted) return;
      final go = await showDialog<bool>(
        context: context,
        builder: (ctx) => AlertDialog(
          title: const Text('Allow installs',
              style: TextStyle(color: AppColors.primaryText, fontSize: 16)),
          content: const Text(
            'To update itself, Sleazy Crow needs permission to install apps.\n\n'
            'Turn on "Allow from this source" on the next screen, come back, '
            'and tap Install again.',
            style: TextStyle(color: AppColors.secondaryText, fontSize: 13)),
          actions: [
            TextButton(onPressed: () => Navigator.of(ctx).pop(false), child: const Text('Cancel')),
            ElevatedButton(onPressed: () => Navigator.of(ctx).pop(true), child: const Text('Open settings')),
          ],
        ),
      );
      if (go == true) await _installer.invokeMethod('openInstallSettings');
      if (!context.mounted) return;
      // Keep the verified file; offer to install once they come back.
      final retry = await showDialog<bool>(
        context: context,
        builder: (ctx) => AlertDialog(
          content: Text('v${update.version} is downloaded and verified.',
              style: const TextStyle(color: AppColors.primaryText, fontSize: 13)),
          actions: [
            TextButton(onPressed: () => Navigator.of(ctx).pop(false), child: const Text('Later')),
            ElevatedButton(onPressed: () => Navigator.of(ctx).pop(true), child: const Text('Install')),
          ],
        ),
      );
      if (retry != true) return;
      if (!(await _installer.invokeMethod<bool>('canInstall') ?? false)) {
        if (context.mounted) {
          ScaffoldMessenger.of(context).showSnackBar(const SnackBar(
            content: Text('Install permission is still off for Sleazy Crow.'),
            backgroundColor: AppColors.error));
        }
        return;
      }
    }
    await _installer.invokeMethod('installApk', {'path': apkPath});
  } on UpdateServiceException catch (e) {
    closeDialog();
    if (context.mounted) {
      ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(content: Text(e.message), backgroundColor: AppColors.error),
      );
    }
  } catch (e) {
    closeDialog();
    if (context.mounted) {
      ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(content: Text('Update failed: $e'), backgroundColor: AppColors.error),
      );
    }
  } finally {
    progress.dispose();
  }
}

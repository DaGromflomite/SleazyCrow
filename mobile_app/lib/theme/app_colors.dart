import 'package:flutter/material.dart';

/// Centralized color system — Sleazy Crow synthwave dark theme
/// Palette sampled from the Sleazy Crow artwork (branding/): neon pink, cyan and gold
/// on a deep purple-black.
class AppColors {
  // Private constructor to prevent instantiation
  AppColors._();

  // ========== Background (purple-black, from the artwork) ==========
  static const Color primaryBackground = Color(0xFF07060F); // Deep purple-black
  static const Color secondaryBackground = Color(0xFF100C1C); // Panels, cards
  static const Color surfaceElevated = Color(0xFF1A1430); // Elements above cards

  // ========== Text ==========
  static const Color primaryText = Color(0xFFF3EAF7); // Warm lilac-white
  static const Color secondaryText = Color(0xFFA897BE); // Muted lavender
  static const Color disabledText = Color(0xFF5B4D70); // Disabled elements

  // ========== Accent (neon pink) ==========
  static const Color primaryAccent = Color(0xFFFF4FA3); // Neon pink
  static const Color accentHover = Color(0xFFFF80BF); // On hover — lighter
  static const Color accentPressed = Color(0xFFD0259E); // On press — deeper magenta

  // ========== Secondary accents ==========
  static const Color cyanAccent = Color(0xFF2EE6D6); // Neon cyan
  static const Color goldAccent = Color(0xFFFCBC4B); // Gold

  // ========== Statuses ==========
  static const Color success = Color(0xFF2EE6A8); // Ready, success (teal-green)
  static const Color warning = Color(0xFFFCBC4B); // Warning, in progress (gold)
  static const Color error = Color(0xFFFF453A); // Error, critical (red, distinct from pink)
  static const Color info = Color(0xFF2EE6D6); // Info (cyan)

  // ========== RF module specific ==========
  static const Color recording = Color(0xFFFF453A); // Signal recording (red)
  static const Color transmitting = Color(0xFFFF4FA3); // Transmitting (neon pink)
  static const Color jamming = Color(0xFFFCBC4B); // Jamming (gold)
  static const Color idle = Color(0xFF2EE6D6); // Ready (cyan)
  static const Color searching = Color(0xFF00F0FF); // Frequency scanning (electric cyan)

  // ========== Borders ==========
  static const Color borderDefault = Color(0xFF2C2244);
  static const Color borderFocus = Color(0xFFFF4FA3);
  static const Color divider = Color(0xFF1F1833);

  // ========== Logs / Console ==========
  static const Color logBackground = Color(0xFF05040C);
  static const Color logText = Color(0xFF3DF5E8); // Neon cyan console
  static const Color logError = Color(0xFFFF453A);
  static const Color logSystem = Color(0xFFFF4FA3);
  static const Color logUserInput = Color(0xFFF3EAF7);

  // ========== Colors for Material ColorScheme ==========
  
  /// Get ColorScheme for dark theme
  static ColorScheme get darkColorScheme {
    return const ColorScheme.dark(
      primary: primaryAccent,
      onPrimary: primaryBackground,
      secondary: primaryAccent,
      onSecondary: primaryBackground,
      error: error,
      onError: primaryText,
      surface: secondaryBackground,
      onSurface: primaryText,
      surfaceContainerHighest: surfaceElevated,
      outline: borderDefault,
      outlineVariant: divider,
    );
  }

  /// Get color for module status
  static Color getModuleStatusColor(String status) {
    switch (status.toLowerCase()) {
      case 'idle':
        return idle;
      case 'recordsignal':
      case 'recording':
        return recording;
      case 'sendsignal':
      case 'transmitting':
        return transmitting;
      case 'jamming':
        return jamming;
      case 'detectsignal':
      case 'searching':
        return searching;
      default:
        return secondaryText;
    }
  }
}


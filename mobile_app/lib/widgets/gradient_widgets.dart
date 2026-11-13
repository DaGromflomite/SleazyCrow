import 'package:flutter/material.dart';
import '../theme/app_colors.dart';

/// Reusable cyberpunk (pink -> purple) UI pieces. Central so the look can be
/// rolled across the whole app from one place.

/// Full-bleed page backdrop: deep purple glow fading into the near-black base.
class GradientBackground extends StatelessWidget {
  final Widget child;
  const GradientBackground({super.key, required this.child});

  @override
  Widget build(BuildContext context) {
    return Container(
      decoration: const BoxDecoration(gradient: AppColors.backgroundGradient),
      child: child,
    );
  }
}

/// Colored (pink -> purple) header banner for the top of a bottom sheet.
/// Carries a grab pill + title (+ optional subtitle) so the drag-to-close area
/// is obvious. Pair with `showDragHandle: false` and a sheet `shape` whose top
/// radius matches [radius].
class GradientSheetHeader extends StatelessWidget {
  final String title;
  final String? subtitle;
  final double radius;

  const GradientSheetHeader(
      {super.key, required this.title, this.subtitle, this.radius = 20});

  @override
  Widget build(BuildContext context) {
    return Container(
      width: double.infinity,
      padding: const EdgeInsets.only(top: 8, bottom: 12),
      decoration: BoxDecoration(
        gradient: AppColors.cyberGradient,
        borderRadius: BorderRadius.vertical(top: Radius.circular(radius)),
      ),
      child: Column(
        mainAxisSize: MainAxisSize.min,
        children: [
          Container(
            width: 44,
            height: 5,
            decoration: BoxDecoration(
              color: Colors.white.withValues(alpha: 0.85),
              borderRadius: BorderRadius.circular(3),
            ),
          ),
          const SizedBox(height: 10),
          Text(
            title,
            style: const TextStyle(
              color: Colors.white,
              fontSize: 18,
              fontWeight: FontWeight.bold,
            ),
          ),
          if (subtitle != null)
            Padding(
              padding: const EdgeInsets.only(top: 2),
              child: Text(
                subtitle!,
                style: const TextStyle(color: Colors.white70, fontSize: 11),
              ),
            ),
        ],
      ),
    );
  }
}

/// Pink -> purple gradient pill button with a soft neon glow.
class GradientButton extends StatelessWidget {
  final VoidCallback? onPressed;
  final IconData? icon;
  final String label;
  final EdgeInsetsGeometry padding;
  final bool expand;

  const GradientButton({
    super.key,
    required this.label,
    this.onPressed,
    this.icon,
    this.padding = const EdgeInsets.symmetric(horizontal: 18, vertical: 12),
    this.expand = false,
  });

  @override
  Widget build(BuildContext context) {
    final enabled = onPressed != null;
    final row = Row(
      mainAxisSize: expand ? MainAxisSize.max : MainAxisSize.min,
      mainAxisAlignment: MainAxisAlignment.center,
      children: [
        if (icon != null) ...[
          Icon(icon, size: 18, color: AppColors.primaryText),
          const SizedBox(width: 8),
        ],
        Text(
          label,
          style: const TextStyle(
            color: AppColors.primaryText,
            fontWeight: FontWeight.w700,
          ),
        ),
      ],
    );

    return Opacity(
      opacity: enabled ? 1.0 : 0.45,
      child: DecoratedBox(
        decoration: BoxDecoration(
          gradient: AppColors.cyberGradient,
          borderRadius: BorderRadius.circular(30),
          boxShadow: enabled
              ? [
                  BoxShadow(
                    color: AppColors.cyberPink.withValues(alpha: 0.35),
                    blurRadius: 14,
                    offset: const Offset(0, 3),
                  ),
                ]
              : null,
        ),
        child: Material(
          color: Colors.transparent,
          child: InkWell(
            borderRadius: BorderRadius.circular(30),
            onTap: onPressed,
            child: Padding(padding: padding, child: row),
          ),
        ),
      ),
    );
  }
}

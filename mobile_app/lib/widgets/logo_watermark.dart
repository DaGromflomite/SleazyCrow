import 'package:flutter/material.dart';

/// A very subtle, greyed-out Sleazy Crow emblem used to fill otherwise-blank
/// areas so they read as "quiet" rather than a flat black wall. The logo is
/// desaturated to grey and drawn at a low opacity, so it sits just a shade off
/// the background. Non-interactive; place it behind content in a Stack, or use
/// [LogoEmptyState] for the common "icon + message" empty view.
class LogoWatermark extends StatelessWidget {
  final double size;
  final double opacity;

  const LogoWatermark({super.key, this.size = 240, this.opacity = 0.05});

  // Luminance-weighted grayscale matrix (ITU-R BT.709).
  static const List<double> _grayscale = <double>[
    0.2126, 0.7152, 0.0722, 0, 0, //
    0.2126, 0.7152, 0.0722, 0, 0, //
    0.2126, 0.7152, 0.0722, 0, 0, //
    0, 0, 0, 1, 0, //
  ];

  @override
  Widget build(BuildContext context) {
    return IgnorePointer(
      child: Opacity(
        opacity: opacity,
        child: ColorFiltered(
          colorFilter: const ColorFilter.matrix(_grayscale),
          child: Image.asset(
            'assets/images/sleazycrow_logo.png',
            width: size,
            height: size,
            fit: BoxFit.contain,
          ),
        ),
      ),
    );
  }
}

/// A centred empty-state: the faint logo watermark with an optional title and
/// message layered on top, so empty lists feel branded instead of blank.
class LogoEmptyState extends StatelessWidget {
  final String? title;
  final String? message;
  final double logoSize;
  final double logoOpacity;

  const LogoEmptyState({
    super.key,
    this.title,
    this.message,
    this.logoSize = 275,
    this.logoOpacity = 0.06,
  });

  @override
  Widget build(BuildContext context) {
    return Center(
      child: Stack(
        alignment: Alignment.center,
        children: [
          LogoWatermark(size: logoSize, opacity: logoOpacity),
          Padding(
            padding: const EdgeInsets.symmetric(horizontal: 32),
            child: Column(
              mainAxisSize: MainAxisSize.min,
              children: [
                if (title != null)
                  Text(
                    title!,
                    textAlign: TextAlign.center,
                    style: const TextStyle(
                      color: Color(0xFFF3EAF7), // primaryText
                      fontSize: 16,
                      fontWeight: FontWeight.w600,
                    ),
                  ),
                if (message != null) ...[
                  const SizedBox(height: 6),
                  Text(
                    message!,
                    textAlign: TextAlign.center,
                    style: const TextStyle(
                      color: Color(0xFFA897BE), // secondaryText
                      fontSize: 13,
                    ),
                  ),
                ],
              ],
            ),
          ),
        ],
      ),
    );
  }
}

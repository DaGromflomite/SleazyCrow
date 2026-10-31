# Sleazy Crow branding

Artwork sources live in `branding/source/` (any size; generated with ComfyUI):

| Source | What it should be |
|---|---|
| `source/emblem.png` | Square logo on a solid background colour (at least 512px). |
| `source/banner.png` | Wide banner, about 2.4:1 (at least 1200px wide). |

Run `python tools/sleazy.py icons` after changing either one. It generates:

- `app-logo-512.png`: emblem with the background removed (also `mobile_app/assets/images/sleazycrow_logo.png`)
- `app-icon-1024.png`: launcher icon preview
- Android launcher icons, legacy and adaptive (`mobile_app/android/app/src/main/res/mipmap-*`), with the icon background taken from the artwork
- `web-flasher/favicon.png` and `web-flasher/banner.png`

`python tools/sleazy.py check --release` refuses to build a release while `PLACEHOLDER`
exists, the sources are missing or the wrong shape, or the icons haven't been generated.

The app colour scheme (`mobile_app/lib/theme/app_colors.dart`) is sampled from this artwork.
Do not use Senape3000's or Joel Serna's logos or artwork for Sleazy Crow branding.

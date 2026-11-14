# Sleazy Crow

![Sleazy Crow](branding/source/banner.png)

**Sleazy Crow** is a community fork of [Senape3000's EvilCrowRF-V2](https://github.com/Senape3000/EvilCrowRF-V2)
firmware and Android app for the [Evil Crow RF V2](https://github.com/joelsernamoreno/EvilCrowRF-V2) hardware.
It keeps everything the original does and focuses on stability and reliability fixes.

> ⚠ **Beta, meant for testing.** If it doesn't work on your Evil Crow, go back to
> [Senape3000's official firmware](https://senape3000.github.io/EvilCrowRF-V2/web-flasher/) and please
> [report what happened](https://github.com/DaGromflomite/SleazyCrow/issues/new).

## Install

- **Firmware:** the web flasher at **https://dagromflomite.github.io/SleazyCrow/** (desktop Chrome, Edge or Opera), or esptool with the files from [Releases](https://github.com/DaGromflomite/SleazyCrow/releases).
- **App:** `SleazyCrow-vX.Y.Z.apk` from [Releases](https://github.com/DaGromflomite/SleazyCrow/releases). It installs as its own app next to the official EvilCrow RF app. The official app also works with Sleazy Crow firmware (same Bluetooth protocol).
- **Updates:** the Sleazy Crow app checks this repo's releases for new firmware (OTA over Bluetooth) and app versions.

## What's different from the original

See [`changelog.json`](changelog.json) for the full list. Highlights:

- Bluetooth no longer drops ~12 s after connecting on Android.
- No boot crash on newer Arduino cores; toolchain pinned so every build is identical.
- **Sub-GHz `.sub` replay now transmits with faithful pulse timing.** Before, each
  pulse was held too long while the next one was read from the SD card, which
  stretched and garbled short-pulse signals — TouchTunes, for example, worked from
  a Flipper but did nothing from the Crow. Now the signal is loaded into RAM first
  and clocked out tightly. Verified over the air against a Flipper Zero: the Crow's
  transmission decodes bit-for-bit identical to the source file, with pulse widths
  within ~1%.
- File uploads can no longer silently save a corrupted file. (Every TouchTunes file
  copied to the SD card was also checked byte-for-byte against the source repo — all
  8,167 matched exactly.)
- App: drill into **any** Flipper Sub-GHz DB subfolder, not just the top-level
  categories, so you copy exactly the folders you want; much faster and more reliable
  uploads; transmit toggle; brute-force frequency/timing options.

Feature documentation for the device itself is in Senape3000's original README: [`docs/UPSTREAM_README.md`](docs/UPSTREAM_README.md).

## Build

```
pio run                                   # firmware -> .pio/build/esp32dev/
cd mobile_app && flutter build apk --release
python tools/sleazy.py package --apk mobile_app/build/app/outputs/flutter-apk/app-release.apk
```

Releases are built by GitHub Actions from a version tag; see [`RELEASING.md`](RELEASING.md).

## Credits

- **Senape3000**: author of the [EvilCrowRF-V2](https://github.com/Senape3000/EvilCrowRF-V2) firmware and app this fork is based on.
- **Joel Serna (joelsernamoreno)**: Evil Crow RF V2 hardware design and original firmware.
- **tutejshy-bit**, **realdaveblanch**, **ProtoPirate**: see the credits in the original project.
- **DaGromflomite**: Sleazy Crow fork maintainer. The problems were diagnosed and the fixes directed by DaGromflomite; the code was written with AI assistance (Claude).

Not affiliated with or endorsed by Senape3000 or Joel Serna.

## License

MIT, see [LICENSE](LICENSE). Original work © 2025–2026 Senape3000; Sleazy Crow modifications © 2026 DaGromflomite.

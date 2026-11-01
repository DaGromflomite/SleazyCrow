# Sleazy Crow — mobile app

The Android companion app for **Sleazy Crow**, a community fork of
[Senape3000's EvilCrowRF-V2](https://github.com/Senape3000/EvilCrowRF-V2) firmware and app
for the [Evil Crow RF V2](https://github.com/joelsernamoreno/EvilCrowRF-V2) hardware.
It controls the device over Bluetooth Low Energy (BLE).

It installs as its own app (`com.sleazycrow.app`, "Sleazy Crow"), alongside the official
EvilCrow RF app. The official app also works with Sleazy Crow firmware (same BLE protocol).

## Features

- **Device discovery**: finds the device by its BLE service UUID (shows as `SleazyCrow`, and
  still recognises the stock `EvilCrow_RF2` name so it works before you reflash).
- **BLE control**: connect, record, transmit, Sub-GHz, ProtoPirate, nRF, brute force, SDR.
- **Flipper Sub-GHz DB**: pick which sections to copy to the SD card; faster, reliable uploads.
- **Firmware/app updates**: checks the [Sleazy Crow releases](https://github.com/DaGromflomite/SleazyCrow/releases).

## Build

Prerequisites: Flutter (see the repo's `RELEASING.md` for the pinned version), Android SDK, JDK 17.

```
cd mobile_app
flutter pub get
flutter build apk --release
```

Release builds are produced by GitHub Actions from a version tag; see
[`../RELEASING.md`](../RELEASING.md). Signing is read from `android/key.properties`
(local, never committed) or CI secrets.

## Project structure

```
mobile_app/
├── lib/
│   ├── main.dart                 # app entry, theme
│   ├── screens/                  # UI screens
│   ├── providers/
│   │   └── ble_provider.dart     # BLE logic
│   └── services/
│       └── update_service.dart   # update checks (DaGromflomite/SleazyCrow)
├── pubspec.yaml
└── README.md                     # this file
```

## Credits

Based on Senape3000's EvilCrowRF-V2 app. Evil Crow RF V2 hardware by Joel Serna. See the
[root README](../README.md) for full credits. Fork maintained by DaGromflomite.

## License

MIT (see [../LICENSE](../LICENSE)). Original work © 2025–2026 Senape3000; Sleazy Crow
modifications © 2026 DaGromflomite.

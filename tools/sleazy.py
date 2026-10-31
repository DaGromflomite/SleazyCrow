#!/usr/bin/env python3
"""Sleazy Crow release tool — one place for versions, checks and packaging.

Used both locally and by .github/workflows/release.yml, so a local test build and a
published release are produced the same way.

  python tools/sleazy.py status                    show versions / branding state
  python tools/sleazy.py set-version 1.2.1 --beta  set firmware + app version together
  python tools/sleazy.py check [--tag v1.2.0-beta] [--release]
                                                   refuse to continue if anything is off
  python tools/sleazy.py icons                     app icons + favicon from branding/
  python tools/sleazy.py package [--apk PATH]      dist/release (assets) + dist/site (flasher)
  python tools/sleazy.py notes                     dist/release-notes.md

Versioning: the firmware reports only MAJOR.MINOR.PATCH to the app, so every beta bumps
PATCH (1.2.0-beta, 1.2.1-beta, ...). The "-beta" label lives in the version string, the
APK versionName and the file names. A final release drops "-beta".
"""
from __future__ import annotations

import argparse
import datetime
import glob
import hashlib
import json
import os
import re
import shutil
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CONFIG_H = ROOT / "include" / "config.h"
PUBSPEC = ROOT / "mobile_app" / "pubspec.yaml"
UPDATE_SERVICE = ROOT / "mobile_app" / "lib" / "services" / "update_service.dart"
CHANGELOG = ROOT / "changelog.json"
FLASHER_SRC = ROOT / "web-flasher"
BRANDING = ROOT / "branding"
BUILD_DIR = ROOT / ".pio" / "build" / "esp32dev"
DIST = ROOT / "dist"

REPO_OWNER, REPO_NAME = "DaGromflomite", "SleazyCrow"
FW_PREFIX, APK_PREFIX = "sleazycrow-fw-v", "SleazyCrow-v"

# Flash layout (must match partitions.csv / ESP32 bootloader offset)
PARTS = [("bootloader.bin", 0x1000), ("partitions.bin", 0x8000),
         ("boot_app0.bin", 0xE000), ("firmware.bin", 0x10000)]

# Branding: artwork sources (any size) in branding/source/; `icons` generates the rest.
EMBLEM_SRC = BRANDING / "source" / "emblem.png"   # square logo on a solid background
BANNER_SRC = BRANDING / "source" / "banner.png"   # wide banner (about 2.4:1)
ANDROID_ICON_SIZES = {"mdpi": 48, "hdpi": 72, "xhdpi": 96, "xxhdpi": 144, "xxxhdpi": 192}
# Adaptive icon layers are 108dp; only the centre 66dp is guaranteed visible.
ADAPTIVE_SIZES = {"mdpi": 108, "hdpi": 162, "xhdpi": 216, "xxhdpi": 324, "xxxhdpi": 432}

VER_RE = re.compile(r"^(\d+)\.(\d+)\.(\d+)(-beta)?$")


def die(msg: str) -> None:
    print(f"ERROR: {msg}", file=sys.stderr)
    sys.exit(1)


def md5(path: Path) -> str:
    return hashlib.md5(path.read_bytes()).hexdigest()


def read(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def write_keep_eol(path: Path, text: str) -> None:
    """Write text, preserving the file's existing line endings."""
    crlf = b"\r\n" in path.read_bytes()
    text = text.replace("\r\n", "\n")
    path.write_bytes((text.replace("\n", "\r\n") if crlf else text).encode("utf-8"))


# ── versions ──────────────────────────────────────────────────────────────────

def fw_version() -> tuple[str, tuple[int, int, int]]:
    s = read(CONFIG_H)
    def num(name: str) -> int:
        m = re.search(rf"#define\s+FIRMWARE_VERSION_{name}\s+(\d+)", s)
        if not m:
            die(f"FIRMWARE_VERSION_{name} missing in config.h")
        return int(m.group(1))
    m = re.search(r'#define\s+FIRMWARE_VERSION_STRING\s+"([^"]+)"', s)
    if not m:
        die("FIRMWARE_VERSION_STRING missing in config.h")
    return m.group(1), (num("MAJOR"), num("MINOR"), num("PATCH"))


def app_version() -> tuple[str, int]:
    m = re.search(r"^version:\s*([0-9A-Za-z.\-]+)\+(\d+)\s*$", read(PUBSPEC), re.M)
    if not m:
        die("pubspec.yaml version must look like 1.2.0-beta+32")
    return m.group(1), int(m.group(2))


def numeric(version: str) -> str:
    return version.split("-")[0]


def cmd_set_version(args) -> None:
    if not re.fullmatch(r"\d+\.\d+\.\d+", args.version):
        die("give the version as X.Y.Z (add --beta for a beta)")
    full = args.version + ("-beta" if args.beta else "")
    major, minor, patch = args.version.split(".")
    s = read(CONFIG_H)
    for name, val in (("MAJOR", major), ("MINOR", minor), ("PATCH", patch)):
        s = re.sub(rf"(#define\s+FIRMWARE_VERSION_{name}\s+)\d+", rf"\g<1>{val}", s)
    s = re.sub(r'(#define\s+FIRMWARE_VERSION_STRING\s+)"[^"]+"', rf'\g<1>"{full}"', s)
    write_keep_eol(CONFIG_H, s)
    _, build = app_version()
    p = re.sub(r"^version:.*$", f"version: {full}+{build + 1}", read(PUBSPEC), count=1, flags=re.M)
    write_keep_eol(PUBSPEC, p)
    print(f"firmware + app set to {full} (app build number {build + 1})")
    if not any(e.get("version") == args.version for e in json.loads(read(CHANGELOG))["firmware"]):
        print(f"REMINDER: add a {args.version} entry to changelog.json (firmware and app)")


# ── checks ────────────────────────────────────────────────────────────────────

def cmd_check(args) -> list[str]:
    problems: list[str] = []
    fw_str, fw_nums = fw_version()
    app_str, _ = app_version()

    m = VER_RE.match(fw_str)
    if not m:
        problems.append(f"firmware version '{fw_str}' is not X.Y.Z or X.Y.Z-beta")
    elif tuple(int(x) for x in m.groups()[:3]) != fw_nums:
        problems.append(f"config.h numbers {fw_nums} don't match string '{fw_str}'")
    if app_str != fw_str:
        problems.append(f"app version '{app_str}' != firmware version '{fw_str}' (use set-version)")
    if args.tag and args.tag != f"v{fw_str}":
        problems.append(f"tag '{args.tag}' != expected 'v{fw_str}'")

    cl = json.loads(read(CHANGELOG))
    for kind in ("firmware", "app"):
        if not any(e.get("version") == numeric(fw_str) for e in cl.get(kind, [])):
            problems.append(f"changelog.json has no '{kind}' entry for {numeric(fw_str)}")

    svc = read(UPDATE_SERVICE)
    if f"githubOwner = '{REPO_OWNER}'" not in svc or f"githubRepo = '{REPO_NAME}'" not in svc:
        problems.append("app update_service.dart does not point at DaGromflomite/SleazyCrow")
    for pattern, sample in (
        (r"r'(sleazycrow-fw-v[^']+)'", f"{FW_PREFIX}{fw_str}-OTA.bin"),
        (r"r'(SleazyCrow-v[^']+)'", f"{APK_PREFIX}{fw_str}.apk"),
    ):
        pm = re.search(pattern, svc)
        if not pm or not re.search(pm.group(1), sample):
            problems.append(f"app update pattern would not find '{sample}'")

    if args.release:
        from PIL import Image
        for path, rule in ((EMBLEM_SRC, "square, at least 512px"), (BANNER_SRC, "wide, at least 1200px")):
            if not path.exists():
                problems.append(f"{path.relative_to(ROOT)} missing (artwork not delivered yet)")
                continue
            with Image.open(path) as im:
                w, h = im.size
            ok = (w == h and w >= 512) if path == EMBLEM_SRC else (w >= 1200 and 2.0 <= w / h <= 3.5)
            if not ok:
                problems.append(f"{path.relative_to(ROOT)} is {w}x{h}; needs to be {rule}")
        launcher = ROOT / "mobile_app/android/app/src/main/res/mipmap-anydpi-v26/ic_launcher.xml"
        if not launcher.exists():
            problems.append("app icons not generated (run: python tools/sleazy.py icons)")
        if (BRANDING / "PLACEHOLDER").exists():
            problems.append("branding/PLACEHOLDER still present: real logos not installed")

    for p in problems:
        print(f"  [FAIL] {p}")
    if problems:
        die(f"{len(problems)} problem(s); nothing was built or published")
    print(f"  [OK] all checks passed for {fw_str}")
    return problems


# ── icons ─────────────────────────────────────────────────────────────────────

def _emblem_layers():
    """Return (transparent emblem cropped to its content, background RGB)."""
    from PIL import Image, ImageChops, ImageDraw
    with Image.open(EMBLEM_SRC) as src:
        im = src.convert("RGB")
    bg = im.getpixel((2, 2))
    # Knock out the solid background by flood-filling from the edges only, so dark
    # areas inside the artwork (the crow's body) stay opaque.
    marker = (255, 0, 255)
    work = im.copy()
    w, h = work.size
    for x, y in [(0, 0), (w - 1, 0), (0, h - 1), (w - 1, h - 1), (w // 2, 0), (w // 2, h - 1),
                 (0, h // 2), (w - 1, h // 2)]:
        if work.getpixel((x, y)) != marker:
            ImageDraw.floodfill(work, (x, y), marker, thresh=40)
    mask = ImageChops.difference(work, Image.new("RGB", work.size, marker)).convert("L")
    alpha = mask.point(lambda v: 255 if v > 0 else 0)
    rgba = im.convert("RGBA")
    rgba.putalpha(alpha)
    return rgba.crop(alpha.getbbox()), bg


def _fit(img, canvas_px, fraction, fill):
    """Centre img scaled to `fraction` of a square canvas filled with `fill`."""
    from PIL import Image
    scale = canvas_px * fraction / max(img.size)
    sized = img.resize((max(1, round(img.width * scale)), max(1, round(img.height * scale))), Image.LANCZOS)
    canvas = Image.new("RGBA", (canvas_px, canvas_px), fill)
    canvas.alpha_composite(sized, ((canvas_px - sized.width) // 2, (canvas_px - sized.height) // 2))
    return canvas


def cmd_icons(_args) -> None:
    from PIL import Image
    for path in (EMBLEM_SRC, BANNER_SRC):
        if not path.exists():
            die(f"{path.relative_to(ROOT)} not found")
    emblem, bg = _emblem_layers()
    res = ROOT / "mobile_app" / "android" / "app" / "src" / "main" / "res"
    bg_hex = "#{:02X}{:02X}{:02X}".format(*bg)

    # Transparent logo: in-app asset + branding preview
    logo = _fit(emblem, 512, 0.96, (0, 0, 0, 0))
    logo.save(BRANDING / "app-logo-512.png")
    logo.save(ROOT / "mobile_app" / "assets" / "images" / "sleazycrow_logo.png")

    # Legacy launcher icon (older Android): emblem on the art's background colour
    legacy = _fit(emblem, 1024, 0.84, bg + (255,))
    legacy.convert("RGB").save(BRANDING / "app-icon-1024.png")
    for density, px in ANDROID_ICON_SIZES.items():
        legacy.resize((px, px), Image.LANCZOS).convert("RGB").save(res / f"mipmap-{density}" / "ic_launcher.png")

    # Adaptive icon (Android 8+): background colour + emblem inside the 66dp safe zone
    for density, px in ADAPTIVE_SIZES.items():
        _fit(emblem, px, 0.60, (0, 0, 0, 0)).save(res / f"mipmap-{density}" / "ic_launcher_foreground.png")
    (res / "mipmap-anydpi-v26").mkdir(exist_ok=True)
    (res / "mipmap-anydpi-v26" / "ic_launcher.xml").write_text(
        '<?xml version="1.0" encoding="utf-8"?>\n'
        '<adaptive-icon xmlns:android="http://schemas.android.com/apk/res/android">\n'
        '    <background android:drawable="@color/ic_launcher_background"/>\n'
        '    <foreground android:drawable="@mipmap/ic_launcher_foreground"/>\n'
        '</adaptive-icon>\n', encoding="utf-8")
    (res / "values" / "ic_launcher_background.xml").write_text(
        '<?xml version="1.0" encoding="utf-8"?>\n<resources>\n'
        f'    <color name="ic_launcher_background">{bg_hex}</color>\n</resources>\n', encoding="utf-8")

    # Web flasher: favicon + banner (banner capped at 1536px wide to keep the page light)
    _fit(emblem, 64, 0.96, (0, 0, 0, 0)).save(FLASHER_SRC / "favicon.png")
    with Image.open(BANNER_SRC) as b:
        b = b.convert("RGB")
        if b.width > 1536:
            b = b.resize((1536, round(b.height * 1536 / b.width)), Image.LANCZOS)
        b.save(FLASHER_SRC / "banner.png", optimize=True)
    print(f"icons, logo, favicon and banner generated (icon background {bg_hex})")


# ── packaging ─────────────────────────────────────────────────────────────────

def find_boot_app0() -> Path:
    home = Path(os.environ.get("PLATFORMIO_CORE_DIR", Path.home() / ".platformio"))
    hits = sorted(glob.glob(str(home / "packages" / "framework-arduinoespressif32*" /
                                "tools" / "partitions" / "boot_app0.bin")))
    if not hits:
        die("boot_app0.bin not found in PlatformIO packages (run `pio run` first)")
    return Path(hits[0])


def cmd_package(args) -> None:
    fw_str, _ = fw_version()
    rel, site = DIST / "release", DIST / "site"
    shutil.rmtree(DIST, ignore_errors=True)
    rel.mkdir(parents=True)

    sources = {name: BUILD_DIR / name for name, _ in PARTS}
    sources["boot_app0.bin"] = find_boot_app0()
    for name, path in sources.items():
        if not path.exists():
            die(f"{path} missing — run `pio run` first")
    fw = sources["firmware.bin"].read_bytes()
    if fw[0] != 0xE9:
        die("firmware.bin does not start with the ESP32 image magic byte")
    if f'"{fw_str}"'.encode() not in fw and fw_str.encode() not in fw:
        die(f"firmware.bin does not contain version string {fw_str} — stale build?")

    # Merged image for 0x0 (gaps padded with 0xFF, exactly like esptool merge-bin)
    end = max(off + sources[n].stat().st_size for n, off in PARTS)
    image = bytearray(b"\xff" * end)
    for name, off in PARTS:
        data = sources[name].read_bytes()
        image[off:off + len(data)] = data
    for name, off in PARTS:
        data = sources[name].read_bytes()
        assert image[off:off + len(data)] == data, name

    ota = rel / f"{FW_PREFIX}{fw_str}-OTA.bin"
    full = rel / f"{FW_PREFIX}{fw_str}-full.bin"
    ota.write_bytes(fw)
    full.write_bytes(image)
    assets = [ota, full]

    if args.apk:
        apk = rel / f"{APK_PREFIX}{fw_str}.apk"
        shutil.copyfile(args.apk, apk)
        assets.append(apk)
    else:
        print("  (no --apk given: firmware-only package)")

    for a in assets:
        Path(f"{a}.md5").write_text(f"{md5(a)}  {a.name}\n", encoding="utf-8", newline="\n")
    shutil.copyfile(CHANGELOG, rel / "changelog.json")

    # Web flasher site: template + firmware parts + manifest
    shutil.copytree(FLASHER_SRC, site)
    fwdir = site / "firmware"
    fwdir.mkdir(exist_ok=True)
    rows = []
    for name, off in PARTS:
        shutil.copyfile(sources[name], fwdir / name)
        rows.append((name, off, md5(fwdir / name)))
    shutil.copyfile(full, fwdir / full.name)
    rows.append((full.name, 0, md5(full)))
    (fwdir / "md5sums.txt").write_text("".join(f"{h}  {n}\n" for n, _, h in rows),
                                       encoding="utf-8", newline="\n")
    manifest = {
        "name": "Sleazy Crow", "version": fw_str, "new_install_prompt_erase": True,
        "builds": [{"chipFamily": "ESP32", "improv": False,
                    "parts": [{"path": n, "offset": o} for n, o in PARTS]}],
    }
    (fwdir / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    table = "\n".join(
        f'      <tr><td><a href="firmware/{n}">{n}</a></td><td>0x{o:x}</td><td><code>{h}</code></td></tr>'
        for n, o, h in rows)
    commit = os.environ.get("GITHUB_SHA", "")[:7] or "local"
    page = (site / "index.html").read_text(encoding="utf-8")
    for key, val in {"{{VERSION}}": fw_str, "{{FULL_BIN}}": full.name,
                     "{{MD5_ROWS}}": table, "{{COMMIT}}": commit}.items():
        page = page.replace(key, val)
    if "{{" in page:
        die("unfilled {{placeholder}} left in web-flasher/index.html")
    (site / "index.html").write_text(page, encoding="utf-8")

    print(f"packaged {fw_str}:")
    for a in sorted(rel.iterdir()):
        print(f"  dist/release/{a.name}")
    print("  dist/site/ (web flasher)")


def cmd_notes(_args) -> None:
    fw_str, _ = fw_version()
    num = numeric(fw_str)
    cl = json.loads(read(CHANGELOG))
    def section(kind: str) -> str:
        for e in cl.get(kind, []):
            if e.get("version") == num:
                return "\n".join(f"- **{c['type']}**: {c['text']}" for c in e.get("changes", []))
        return "- (no entry)"
    beta = fw_str.endswith("-beta")
    warning = (f"> ⚠ **BETA — for testing.** If it doesn't work on your Evil Crow, go back to "
               f"[Senape3000's official firmware](https://senape3000.github.io/EvilCrowRF-V2/web-flasher/) "
               f"and please [report what happened](https://github.com/{REPO_OWNER}/{REPO_NAME}/issues/new).\n"
               if beta else "")
    notes = f"""# Sleazy Crow v{fw_str}

{warning}
Sleazy Crow is a community fork of [Senape3000's EvilCrowRF-V2](https://github.com/Senape3000/EvilCrowRF-V2) firmware and app for the Evil Crow RF V2 (MIT license).

## Install
- **Web flasher (easiest):** https://{REPO_OWNER.lower()}.github.io/{REPO_NAME}/ — desktop Chrome/Edge/Opera.
- **App:** install `{APK_PREFIX}{fw_str}.apk` on Android. It installs as a separate app ("Sleazy Crow") next to the official one.
  **Known issue:** the first launch after installing can show a Flutter error. Close and reopen the app (or restart the phone); it then starts normally.
- **esptool:** `esptool --chip esp32 erase-flash` then `esptool --chip esp32 --baud 921600 write-flash 0x0 {FW_PREFIX}{fw_str}-full.bin`
- **OTA from the app:** the app finds `{FW_PREFIX}{fw_str}-OTA.bin` automatically.

## Firmware changes
{section("firmware")}

## App changes
{section("app")}

Checksums: each file has a matching `.md5`.
"""
    DIST.mkdir(exist_ok=True)
    (DIST / "release-notes.md").write_text(notes, encoding="utf-8")
    print("wrote dist/release-notes.md")


def cmd_status(_args) -> None:
    fw_str, nums = fw_version()
    app_str, build = app_version()
    print(f"firmware: {fw_str}  (numeric {nums})")
    print(f"app:      {app_str}+{build}")
    print(f"tag to use: v{fw_str}")
    missing = [n for n in BRAND_FILES if not (BRANDING / n).exists()]
    print("branding: " + ("PLACEHOLDER (logos pending)" if (BRANDING / "PLACEHOLDER").exists()
                          else "ok") + (f"; missing {missing}" if missing else ""))


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("status").set_defaults(fn=cmd_status)
    p = sub.add_parser("set-version"); p.add_argument("version"); p.add_argument("--beta", action="store_true")
    p.set_defaults(fn=cmd_set_version)
    p = sub.add_parser("check"); p.add_argument("--tag"); p.add_argument("--release", action="store_true")
    p.set_defaults(fn=cmd_check)
    sub.add_parser("icons").set_defaults(fn=cmd_icons)
    p = sub.add_parser("package"); p.add_argument("--apk"); p.set_defaults(fn=cmd_package)
    sub.add_parser("notes").set_defaults(fn=cmd_notes)
    args = ap.parse_args()
    args.fn(args)


if __name__ == "__main__":
    main()

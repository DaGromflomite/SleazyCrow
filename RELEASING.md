# Releasing Sleazy Crow

Everything is automated as far as possible. `tools/sleazy.py check` stops the process if
anything is inconsistent, and the GitHub Actions pipeline (`.github/workflows/release.yml`)
builds, verifies and publishes from a version tag. Nothing is published by building locally.

## Version rules

- Firmware and app always share one version: `X.Y.Z-beta` for betas, `X.Y.Z` for finals.
- The firmware tells the app only the numbers `X.Y.Z`, so **every beta bumps Z**:
  `1.2.0-beta` → `1.2.1-beta` → … → final `1.3.0`.
- The git tag is always `v` + the version, e.g. `v1.2.0-beta`.
- A tag or published file is never reused or replaced. Mistake? Release the next version.

## A. First publish (one time)

Prerequisites:
- [ ] Artwork in `branding/source/`, `python tools/sleazy.py icons` run, `branding/PLACEHOLDER` deleted (see `branding/README.md`)
- [ ] App colour scheme updated from the logo (`mobile_app/lib/theme/app_colors.dart`)
- [ ] APK built locally and tested on a phone (connect, upload, transmit, update check)
- [ ] `python tools/sleazy.py check --release` passes
- [ ] Signing key folder (`~/SleazyCrow-signing`, outside the repo) backed up somewhere safe

Then run `bash tools/first_publish.sh`. It does the steps below in order and stops on any error:
1. Forks `Senape3000/EvilCrowRF-V2` as **DaGromflomite/SleazyCrow** (proper GitHub fork, shows "forked from").
2. Sets the description and homepage, and turns on Issues (forks start with them off).
3. Pushes the `sleazycrow` branch to the fork's `main` (a fast-forward on top of Senape3000's history).
4. Stores the signing key as encrypted Actions secrets (`SLEAZY_KEYSTORE_B64`, `SLEAZY_KEYSTORE_PASSWORD`).
5. Switches GitHub Pages to "deploy from Actions".
6. Tags `v<version>` and pushes it, which starts the release pipeline.
7. Waits for the pipeline, then downloads every release file and checks it against its `.md5`.

Afterwards, by hand:
- [ ] Open https://dagromflomite.github.io/SleazyCrow/ and check the page and the Install button.
- [ ] Install the released APK and use **Check for updates** (it should say up to date).
- [ ] Old repo `DaGromflomite/EvilCrowRF-V2`: point its README and release notes at SleazyCrow, turn off its Pages, then archive it (Settings → Archive).

## B. Every new beta

```
python tools/sleazy.py set-version 1.2.1 --beta   # firmware + app together
#   edit changelog.json: add a "1.2.1" entry under both "firmware" and "app"
python tools/sleazy.py check --release
pio run                                            # build + test firmware locally
cd mobile_app && flutter build apk --release       # build + test APK locally
git commit -am "Sleazy Crow 1.2.1-beta"
git tag v1.2.1-beta
git push origin main v1.2.1-beta                   # pipeline builds and publishes
```

The app's update check finds the new firmware and APK automatically once the release is up.

## C. Final (non-beta) release

Same as B, but `set-version X.Y.Z` without `--beta`. The release is then not marked as a pre-release.

## D. What the pipeline checks before publishing

- Tag matches the firmware and app versions; firmware numbers match the version string.
- `changelog.json` has firmware and app entries for this version.
- The app's update source is `DaGromflomite/SleazyCrow` and its file patterns match the release file names.
- Real logos are installed (no placeholder, correct sizes).
- The firmware image is valid and contains this version; the merged image matches its parts byte for byte.
- The APK is signed with the Sleazy Crow key (certificate SHA-256 `9ff56c56…5207af`). A debug-signed APK is refused.

## E. Never

- Never commit `key.properties`, `*.p12` or the password file (`.gitignore` blocks them).
- Never lose the signing key: without it, new APKs can't update installed ones.
- Never use Senape3000's or Joel Serna's logos for Sleazy Crow. Do keep their credit and the LICENSE.
- Never rename the device's Bluetooth service UUID: both apps find the Crow by it.

## Pulling in Senape3000's future changes

```
git remote add upstream https://github.com/Senape3000/EvilCrowRF-V2.git   # once
git fetch upstream && git merge upstream/main
```
Resolve conflicts, then release as a new beta.

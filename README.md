# EmuCoreV

[![Get it on Google Play](https://img.shields.io/badge/Google_Play-EmuCoreV-01875f.svg?logo=google-play&logoColor=white)](https://play.google.com/store/apps/details?id=com.sbro.emucorev)
[![License: GPL v2+](https://img.shields.io/badge/License-GPL%20v2%2B-blue.svg)](LICENSE)
[![Support on Patreon](https://img.shields.io/badge/Patreon-Support%20EmuCoreV-ff424d.svg?logo=patreon&logoColor=white)](https://www.patreon.com/c/emucore/membership)
[![Join Discord](https://img.shields.io/badge/Discord-Join%20our%20server-5865F2.svg?logo=discord&logoColor=white)](https://discord.gg/c5EBeNRpz2)

<a href="https://play.google.com/store/apps/details?id=com.sbro.emucorev"><img alt="Get it on Google Play" src="https://play.google.com/intl/en_us/badges/static/images/badges/en_badge_web_generic.png" height="60"/></a>

EmuCoreV is a PlayStation Vita emulator for Android. It combines a custom Android interface, library and catalog UX, installer flows, and runtime controls with a Vita3K-based emulation core adapted for this app.

![Status](https://img.shields.io/badge/Status-Early%20Development%20%2F%20Unstable-red)

> [!WARNING]
> EmuCoreV is still under active development. Expect instability, renderer-specific issues, incomplete compatibility, and device-to-device performance differences.
>
> Current Android builds target `arm64-v8a` only. Firmware handling, game installation, compatibility sync, and mobile UX are still being refined.
>
> Not all games boot or behave correctly yet. Compatibility, fixes, and performance work are ongoing.

## Highlights

- Vita3K-based native core integrated into a Kotlin + Jetpack Compose Android app
- Library and catalog screens with cover art, metadata, media, and compatibility badges
- Firmware, update, `VPK`, `ZIP`, `PKG`, `zRIF`, `RIF`, and `work.bin` install flows
- In-game overlay, per-game settings, and Android-first storage and setup flows
- App language selection and localized UI resources

## What This Repository Contains

This repository contains the Android application, Compose UI, JNI bridge code, bundled Vita3K source tree, catalog assets, and supporting build tooling used by EmuCoreV.

## Tech Stack

- Kotlin + Jetpack Compose
- Android DataStore
- JNI bridge to native C++
- Vita3K-based emulation core and Android integration layer
- Android NDK + CMake
- Local compatibility sync against Vita3K compatibility data

## Current App Scope

EmuCoreV currently targets Android with:

- `minSdk 28`
- `targetSdk 37`
- package id `com.sbro.emucorev`
- version `0.2.1` (build `72`)
- ABI `arm64-v8a`

## Building Locally

### Requirements

- Android Studio with Android SDK, NDK `29.0.14206865`, and CMake `3.22.1+`
- JDK 21 (Android Studio's bundled JBR is supported)
- Python 3 available to CMake; use `python3.path` in `local.properties` when it is not on `PATH`
- OpenSSL Android outputs prepared under `tools/openssl-test/out`
- the `third_party/vcpkg` checkout initialized

### Debug Build

```powershell
.\gradlew :app:assembleDebug
```

### Notes For Native Dependencies

- `app/build.gradle.kts` currently points `OPENSSL_ROOT_DIR` to `tools/openssl-test/out`
- `tools/build_android_openssl.sh` is the local helper used to prepare the expected OpenSSL layout
- Gradle passes the bundled `third_party/vcpkg` checkout and Android NDK to CMake automatically

## Project Structure

- `app/` Android application module
- `app/src/main/java/com/sbro/emucorev` Kotlin app code
- `app/src/main/cpp` Native bridge and Vita3K-based sources
- `app/src/main/assets` Catalog data and bundled assets
- `tools/` Local helper scripts and ignored machine-specific native build inputs

## Notes

- Firmware files, licenses, keys, and game content are not distributed with this repository.
- Use only your own legally obtained firmware and game dumps.
- Compatibility data shown in the catalog and detail screens is derived from Vita3K's public compatibility data.
- Releases marked as "parallel" are identical to the primary build but use an alternate package ID to allow side-by-side installation.

## Credits

EmuCoreV builds on the Vita3K project and its ecosystem, then layers a custom Android interface, catalog, installer flow, storage handling, and handheld-focused UX on top.

- Vita3K: https://github.com/Vita3K/Vita3K
- Vita3K Compatibility: https://github.com/Vita3K/compatibility
- Vita3K Compatibility Page: https://vita3k.org/compatibility.html?lang=en

## Support

If you want to support ongoing development or join the community:

- Official website: https://emucorev.web.app
- Patreon: https://www.patreon.com/c/emucore/membership
- Discord: https://discord.gg/c5EBeNRpz2
- More apps by the author: https://play.google.com/store/apps/dev?id=7136622298887775989

<a href="https://play.google.com/store/apps/details?id=com.sbro.emucorev"><img alt="Get it on Google Play" src="https://play.google.com/intl/en_us/badges/static/images/badges/en_badge_web_generic.png" height="60"/></a>

## License

This project includes and derives from GPL-licensed Vita3K code, so the repository is distributed under the GNU General Public License v2.0 or later.

See [LICENSE](LICENSE) for details.

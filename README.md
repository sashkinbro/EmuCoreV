# EmuCoreV

[![License: GPL v2+](https://img.shields.io/badge/License-GPL%20v2%2B-blue.svg)](LICENSE)
[![Support on Patreon](https://img.shields.io/badge/Patreon-Support%20EmuCoreV-ff424d.svg)](https://www.patreon.com/c/emucore/membership)
[![Join Discord](https://img.shields.io/badge/Discord-Join%20the%20server-5865F2.svg)](https://discord.gg/c5EBeNRpz2)

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
- `targetSdk 36`
- package id `com.sbro.emucorev`
- version `0.0.2`
- ABI `arm64-v8a`

## Building Locally

### Requirements

- Android Studio with Android SDK, NDK `29.0.14206865`, and CMake `3.22.1+`
- JDK compatible with the Gradle configuration in this project
- OpenSSL Android outputs prepared under `tools/openssl-test/out`
- `vcpkg` installed locally and exposed through `VCPKG_ROOT`

### Debug Build

```powershell
.\gradlew :app:assembleDebug
```

### Notes For Native Dependencies

- `app/build.gradle.kts` currently points `OPENSSL_ROOT_DIR` to `tools/openssl-test/out`
- `tools/build_android_openssl.sh` is the local helper used to prepare the expected OpenSSL layout
- `VCPKG_ROOT` should point to your local `vcpkg` checkout

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

## Credits

EmuCoreV builds on the Vita3K project and its ecosystem, then layers a custom Android interface, catalog, installer flow, storage handling, and handheld-focused UX on top.

- Vita3K: https://github.com/Vita3K/Vita3K
- Vita3K Compatibility: https://github.com/Vita3K/compatibility
- Vita3K Compatibility Page: https://vita3k.org/compatibility.html?lang=en

## Support

If you want to support ongoing development or join the community:

- Patreon: https://www.patreon.com/c/emucore/membership
- Discord: https://discord.gg/c5EBeNRpz2
- More apps by the author: https://play.google.com/store/apps/dev?id=7136622298887775989

## License

This project includes and derives from GPL-licensed Vita3K code, so the repository is distributed under the GNU General Public License v2.0 or later.

See [LICENSE](LICENSE) for details.

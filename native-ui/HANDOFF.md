# Native Vita shell — handoff (2026-11-21)

The native PS Vita shell (`native-ui/`) now replaces the 3D carousel library mode.
Everything below is pushed to `main`.

## Architecture
- `native-ui/shell` = `:native-ui-shell` library (C++ `shell.cpp`, `app_screen.h`,
  `at9_decode.cpp`; Kotlin `NativeShellActivity`, `ShellApps.kt`, `TrophyCollectionApp`,
  `SettingsApp`, `QuickSavesApp`, `ShellThemes`). Always included; it bundles NO
  firmware-derived data: the resource pack is extracted on the device from the installed
  firmware (`FirmwarePack`, `native-ui/extract`, README "Resource pack"), and the app only
  offers the mode when firmware is installed (`NativeLibraryMode.available(context)`),
  otherwise the Compose carousel stays (`app/src/nativeShellStub` is now only a
  fallback source set for builds without the module).
- `native-ui/preview` = thin fixture host (`-PnativeUiPreview=true`).
- App host: `app/src/nativeShell/.../NativeShellHostActivity.kt`, process `:native_shell`.
  `NativeLibraryMode.intent()` points to it; games launch via `VitaLaunchBridge`
  (marker `NativeLibraryMode.ReturnTarget`), the Emulator returns to the shell, the
  shell session (unlocked, open LiveAreas, focus) survives surface loss.
- Main app screens (achievements, settings) are NOT changed; Trophies only reads
  `TrophyRepository`, Quick Saves reads save files directly (core never loaded in shell).
- All `native_ui_*` strings live in the app's 18 `strings.xml`; the shell module
  mirrors them at build time (missing translation = build error).

## Done (verified on Lenovo HA28QVYS, partly OnePlus)
- Title labels matched to SceShell (ltn0 19px + 0.5px embolden, 23px pitch, last
  baseline 92px below bubble, stay on slot — no sway shimmer).
- Adaptive full-screen canvas (544 rows / 960 cols), cutout + rounded-corner safe
  area, orb flush in the corner and enlarged on rounded panels, lock bezel bleed.
- Notification orb balloon (firmware geometry/string).
- Animations no longer freeze during touch (frame clock fix); hold-to-edit works.
- Trophy Collection, Settings (themes, Home/Start backgrounds, System Music,
  System Sounds, Display Area page, time format, System Info + "3.74 EmuCore
  Edition" easter egg, return to library), Quick Saves (own icon/LiveArea).
- Themes: Default + 16 standard wave colour themes (localized), custom Vita theme
  ZIP install (theme.xml backgrounds, start screen, date/bar colours, m_waveType,
  BGM .at9 via LibAtrac9), delete with confirmation, photo backgrounds,
  edit-mode wallpaper tool opens the image picker.
- LiveArea: per-<lang> live items, origin="image" text, own translations when the
  firmware template lacks the language; Start capsule widens for long labels.
- Build: compileSdk 37.1 for the user's dependency bump; FeedbackScreen imports
  the material3 1.5 `ExposedDropdownMenu` extension.

## Open / provisional
- Exact timings (notification fade 200 ms, app open 300 ms zoom, page slide 250 ms)
  are estimates; reference capture needs the emulated SceShell (Anbernic RG556
  has a debug build with firmware — ask the user before using it; Lenovo has a
  release-signed build, never uninstall it: it would wipe games/firmware).
- m_waveType -> effect: traced (libpaf table 0x813012e8 via ScePafGraphics 2E30F1B5;
  out of range = default). Type 31 (waveparam.bin custom wave) uses the bundled preset.
- Theme BGM audio quality not listened to; Quick Saves resume not tested (no saves
  on the device). Monthly Default wave colour: shell.self/libpaf contain no month
  logic (all RTC callers and wave-type writers checked); if it exists it is data in
  vs0:data/internal/theme (only in the installed firmware, check on a device).
- Vita "Hidden trophy" name string and grade names are our translations.

## How to build / test
- `JAVA_HOME="C:\Program Files\Android\Android Studio1\jbr" ./gradlew :app:assembleRelease`
  (release-signed, installs over the Lenovo app), preview:
  `./gradlew -PnativeUiPreview=true :native-ui-preview:assembleDebug`.
- Firmware references: `artifacts/vs0-apps`, `artifacts/rco-dump` (RCO JSON/strings),
  emulated SceShell screenshots in `tools/shell-research/*.png`.
- Bash tool mangles backslashes in heredocs: write Python edit scripts to files.

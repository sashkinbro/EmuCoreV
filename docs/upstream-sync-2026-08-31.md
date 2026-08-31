# Vita3K sync — 2026-08-31

## Sources

- Official Vita3K: `496939b602703951277263c7b3e60a9ae36879c1`; no new commits since the 2026-08-20 integration.
- [Vita3K-Plus code branch](https://github.com/nckstwrt/Vita3K-Plus/tree/2738dcfae5f36af1ed52a089d0253a519a78207e): `b8f834741e97f04d441e0dc7bad6b33d0505d031` → `2738dcfae5f36af1ed52a089d0253a519a78207e` (28 commits).
- Plus `master`: `3a97dd0de459489e2d25bd4f1ec5b5f4c0bac76f`; new commits there are README/issue-template changes, not the game-fix implementation.

## Integrated runtime changes

The cumulative core delta includes Vulkan depth/typeless copies, page-table and external mapping fixes, Mali fallback/atomic checks, shader translation, NGS/ATRAC9/envelopes, memory reclamation, library-scoped NID binding, event-flag hang recovery, archive/SFO handling, and Android diagnostics. Upstream commit descriptions identify LBP, MGS3, Killzone level 5, DOA5, UPPERS, Gundam Breaker 3, Ragnarok ACE, Ys Celceta, Hyperdimension Rebirth1 and AC3 Liberation as affected games. These are upstream claims, not device-tested compatibility results for EmuCoreV.

Front/back/both touch mode is wired through EmuCoreV's JNI bridge and Compose overlay. The previously added pointer ownership, cancellation, report bounds and viewport validation remain intact. Native Buffer is **not** made the global default for unrelated games.

## Deliberate adaptations

- Kept EmuCoreV branding, updater URLs, storage paths, ANGLE integration, shader-cache options and async-pipeline default. Includes the user's version update to `0.2.0` / build `71`.
- Kept official decoder error codes/context-size validation and NGS resume behavior already integrated in August. Added the new AT9 null-info guard without replacing the official implementation with the fork's differing error constants.
- Corrected Plus commit `64db4ff0`: valid SFO magic remains `0x46535000`, and `get_param_info` explicitly returns success. Added bounds/termination checks before parsing SFO tables, keys and values.
- Android memory-pressure callbacks use a process-lifetime atomic mailbox, not a renderer pointer that can be destroyed during session teardown. GPU cache reclamation still happens on the renderer thread.
- Adapted startup/exit diagnostics to our package and bounded tombstone reads to 256 KiB.
- Kept our existing DocumentsProvider. The fork's Java overlay pointer-filtering implementation is not copied over our separate Compose/SDL input routing; the core touch changes are integrated.
- Desktop branding/resources and fork-specific release metadata are intentionally not imported. Required native build changes (codec linkage and return-type errors) are included.
- Removed the fork's unavailable `util/fork_build.h` dependency and fork-build log metadata; kept crash diagnostics with a single terminate handler, avoiding handler chaining across repeated launches.

## FIFA Game DB recommendations (corrected after regression report)

- FIFA Football/Soccer, FIFA 13/14/15, demos, and renamed mods retaining a known FIFA title ID request **Vulkan + native-buffer** at launch.
- 31 regional/demo IDs are sourced from [Vita3K compatibility reports](https://github.com/Vita3K/compatibility/issues?q=FIFA). Case-insensitive FIFA-name matching covers other regions/series names not yet listed, without matching unrelated words.
- Stored in `app/src/main/assets/compatibility/game-db.xml`, shared by Android and the native core. Order: global settings → Game DB defaults → explicit per-game XML. All renderer/buffer choices remain available; recommendations never select or replace a driver.
- The initial integration incorrectly applied a hard override after user XML and hid other choices. That behavior has been removed. Existing saved profiles are preserved, not reset: previously overwritten choices cannot be reliably reconstructed. Removing a custom profile restores the Game DB defaults on top of global settings.
- Global settings and other per-game options are unchanged. Backend capability checks and the upstream safe fallback remain active on devices that cannot support Native Buffer; this is not a guarantee that every GPU can execute that mode or that every FIFA game is fully playable.

See [the follow-up regression audit](fifa-input-launch-regressions-2026-08-31.md) for the targeted stock-driver compatibility correction, touch input and launch preparation changes. The game-specific core fixes listed below are retained.

## Reviewed Plus commits

```text
18e0290d Update version files
c7555734 Fix signal semaphore validation errors and FSR
1d4d85e7 Fix the LBP background properly. Full fix for VPCK. Extra diagnostics
34e8c9bb Memory management check for Mali. Updater points at my repository
778e0927 Fallbacks for Mali - go to DoubleBuffer when NativeBuffer can't work
36d3ffff Big changes. Hang breaker for MGS3. Audio/Video improvements. Blending with MSAA fixes. Logging Enhancements
ed63438f Minor fixes from the latest upstream PRs
d508ff2b Fixes (and logging) for Ys Celceta
30036d3b Fixes for Ragnarok ACE
4ee70305 Memory effiencies (especially for Android)
9c044c8d Better crash logging and ensure SPIRV 1.0 for Mali
53d78e23 Lots of memory efficiences, logging and bug fixes. Fixes Hyperdimension Rebirth1. Attempted fix for MGS3 touchpad too
a3107bf2 Fix for Killzone Level 5. Proper fix for MGS3 touchpad issue. Better memory logging (AGAIN!)
155500b2 DOA5 ix for black clothes on Android
89ac3aa0 More MGS hanging fixes
8629832e Minor audio and memory fixes
04836fa0 Missing build and associated file changes
a87c97df Big Changes. Memory protections. NGS overhaul for AC3L
64e11b23 Slightly better MGS handling, plus atrac9 commandline option
706ae33e Shader fixes for UPPERS
f3c7756d Stop UPPERS from crashing when installing from zip
53f5e415 Support a F+B touchpad - mainly for AC3L
2f726111 Better install logging and fixing a whoops on SFO
d4586321 Add DocumentsProvider for Android file browsing
64db4ff0 Reverse whoops
31b4fed8 Gundam Breaker 3 fixes
8cb139f6 Page Table Fixes
2738dcfa Fix Depth
```

## Validation

- `gradlew.bat testDebugUnitTest`: 97 tests passed, including F+B JNI and trim-callback contracts.
- `powershell -NoProfile -ExecutionPolicy Bypass -File tools/test_native_regressions.ps1`: 53 checks passed, compiling the actual SFO parser and FIFA classifier with MSVC. No Android device is needed.
- `gradlew.bat assembleDebug lintDebug --no-daemon`: arm64 APK built successfully. Lint ran, but is not clean: 64 errors and 364 warnings remain (the project sets `abortOnError = false`). These include SDL Bluetooth permissions/API guards and existing resource/Compose issues; do not interpret the successful Gradle exit as a clean lint report.
- No tablet/ADB access or on-device gameplay testing. Real-game/GPU behavior still needs testing on a free device.

Existing uncommitted `release/baselineProfiles/*`, `release/output-metadata.json`, and `IGDB/` are outside this change. The user explicitly requested including their concurrent version edit in `app/build.gradle.kts` (`0.2.0` / `71`).

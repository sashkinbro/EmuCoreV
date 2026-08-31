# FIFA, touch input and launch regression audit — 2026-08-31

Baseline: `984ec188` on `main`. Version remains the user's `0.2.0` / build `71`.

## Report and scope

The user reported a FIFA black screen with sound on the system GPU driver, then confirmed correct rendering and gameplay with a custom driver. No device, GPU model, title ID or failing log was supplied. No tablet/ADB access was used. This establishes a driver-dependent symptom, not proof of a specific GPU failure.

There is no wholesale upstream rollback. The imported shader/game fixes, depth/typeless copies, NGS/ATRAC changes and F+B touch support are retained. Changes below target independently identified regressions.

## Game DB and user settings

- Removed the duplicated Kotlin/C++ FIFA hardcoding and the UI's single-option lists.
- Added a shared, bundled `compatibility/game-db.xml` containing the existing 31 regional/demo IDs and family-name matcher. Recommendations apply before custom XML, in both layers. No driver is selected by the DB.
- All five buffer choices and both renderers remain selectable and survive loading/launching. Sparse native GPU XML retains inherited renderer/mapping fields instead of blanking them.
- Existing custom profiles are not deleted or automatically reset. The old implementation may already have saved forced values; guessing the user's previous choices would risk further data loss.
- Android parser setup avoids unsupported Xerces feature URIs; [AOSP's DocumentBuilderFactoryImpl](https://android.googlesource.com/platform/libcore/+/refs/heads/main/luni/src/main/java/org/apache/harmony/xml/parsers/DocumentBuilderFactoryImpl.java) only implements namespaces/validation features. External entity resolution is disabled for the bundled asset.

## Native Buffer / driver compatibility

Comparison with the pre-sync code identified a new unconditional `HOST_CACHED` requirement both in Native Buffer eligibility and imported memory-type selection. That can reject a previously accepted coherent import, silently selecting Double Buffer or page-table fallback even when the user requested Native Buffer. It is a plausible explanation of the reported driver difference; confirmation requires the system-driver log or device retest.

- Native Buffer eligibility again depends on required Vulkan mapping features and the AHB/FD import extension, not the presence of a Vulkan host-cached heap.
- Import selection prefers coherent+cached memory, then accepts coherent memory without `HOST_CACHED`. Noncoherent and empty import masks are still rejected.
- The actual `AHardwareBuffer_lock` pointer still undergoes the upstream ARM64 atomic probe before it is exposed to guest code. The page-table fallback now also probes its actual pointer before remapping, avoiding an unsafe uncached fallback.
- Page Table's existing cached-memory requirement remains intact. Unsupported requested mappings now produce an explicit requested-versus-selected log message.

The [Khronos memory-property definitions](https://docs.vulkan.org/refpages/latest/refpages/source/VkMemoryPropertyFlagBits.html) distinguish host coherence from host caching. The [AHB extension](https://docs.vulkan.org/refpages/latest/refpages/source/VK_ANDROID_external_memory_android_hardware_buffer.html) also distinguishes Vulkan host mapping from the native lock API. This change does not claim that every driver/format or every FIFA title is now device-tested.

## Touch controls

- Grouped controls use a tested finger-to-button tracker. Lifting one finger cannot release a button held by another; sliding and cancellation release the correct buttons.
- Cleanup is keyed to the actual tracker/layout dimensions. Previously an effect keyed to `Unit` could retain the first pointer maps while new maps were created during layout changes, leaving later presses unreleased.
- Pausing or losing window focus immediately sends `ACTION_CANCEL` through the Activity's existing Compose/SDL dispatch, preserving all pointer IDs. It does not wait for a paused Compose frame to release held input. Snapshots are recycled and no allocation is added to move events.
- Controls are removed while the lifecycle is not resumed and controller-attachment retries resume with it.
- L2/R2 release uses SDL's full joystick-axis minimum, matching the bundled virtual-controller initialization; zero represented a half-depressed trigger rather than fully released. Invalid negative button IDs are ignored.

These changes do not replace the touch layout, virtual controller attachment, physical controller routing, F/B/F+B mode or keyboard fallback.

## Launch latency

- Firmware checks, profile preparation and native frontend loading run on an IO dispatcher. Activity launch remains on Main. A shared in-flight guard rejects duplicate dispatches, and cancellation is propagated.
- The launch button displays loading immediately and is disabled during preparation; Home/Library launches also show progress. Actual initialization still takes time: no claim of zero latency or a measured device speedup is made.
- Removed the unconditional extra native full-library scan before `startActivity`. Frontend initialization/installers maintain the list; native launch refreshes it on a miss, retaining support for manually copied games.
- A title lookup reads its own SFO directly for normal installs instead of parsing the whole library; the existing scan fallback retains nonstandard-folder/case support.
- Profile synchronization returns its effective config to avoid an extra reload, and a launch preparation duration is logged for subsequent measurements.

## Verification

- `testDebugUnitTest`: **113 tests, 0 failures/errors**. Includes Game DB data/matching/precedence contracts, multitouch tracker behavior, launch threading/cancellation, native atomic guards, trigger ranges and existing IME regressions.
- `tools/test_native_regressions.ps1`: **89 checks passed**, compiling the actual shared Game DB helper, Vulkan memory selector, SFO and IME code with MSVC and the bundled pugixml implementation.
- `assembleDebug`: successful arm64 APK. Inspected the APK ZIP: `assets/compatibility/game-db.xml` exactly matches the source asset and `lib/arm64-v8a/libVita3K.so` is present.
- `minifyReleaseWithR8` and `optimizeReleaseResources`: successful. Inspected the resulting release DEX: `Emulator.dispatchTouchEvent`, cancellation helper and native IME methods, plus `InputOverlay.attachController/setAxis/setButton/setTouchState`, are retained.
- `lintDebug`: completed, but **not clean**: 64 errors / 363 warnings remain. The error count is unchanged from the pre-fix audit; touched launch/Activity code only reports pre-existing ANGLE dynamic loading and URI-style warnings. Gradle uses `abortOnError = false`.
- `git diff --check`: passed.

Host tests cannot verify physical touchscreen dispatch, stock-driver rendering or actual launch time on the user's device. A debug APK was assembled; the release check above covers shrinking/resource optimization, not assembly or installation of a release APK. No device was connected or controlled.

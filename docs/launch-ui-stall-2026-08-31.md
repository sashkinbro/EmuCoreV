# Game-card launch UI stall — 2026-08-31

Baseline: `294916c04f051919c500507c9175f0d99f7f5224` on `main`.
Version remains `0.2.0` / build `71`. No tablet or ADB access was used.

## Confirmed cause

The earlier IO-dispatcher/library-scan changes did not cover the Activity's SDL
window-style path. The card click gate suppresses duplicate clicks; it does not
delay the first click. Native frontend initialization normally already happens
in `EmuCoreVApp.onCreate`.

The actual Kotlin SDL wrapper (`app/src/main/java/org/libsdl/app/SDLActivity.kt`)
enqueues `COMMAND_CHANGE_WINDOW_STYLE`, then waits on the Activity monitor for up
to 500 ms when the surface dimensions do not yet match fullscreen. Both the
queued command and `SDLSurface.surfaceChanged` (which notifies that monitor) need
the UI thread. Calling this path from that same thread therefore blocks the work
that could complete the wait.

At startup, `SDLActivity.onCreate` calls `setWindowStyle(false)` (converted to
fullscreen by `Emulator.shouldForceFullscreen`). `Emulator.onCreate` and
`Emulator.onResume` also call `hideSystemBars` / `setWindowStyle(true)`. Before
surface layout, three such waits add approximately 1.5 seconds. Focus callbacks
can request the style again.

## Narrow fix

Only callers outside `commandHandler.looper` may enter the existing resize-wait
block. UI requests are still queued in the same order, but return immediately so
the Looper can apply the style and deliver layout/surface callbacks. Native
callers retain the existing 500 ms maximum wait and surface notification.

No renderer, driver, Game DB/profile, input, keyboard, native initialization or
storage-migration behavior is changed in this follow-up. The imported game fixes
and previous Native Buffer compatibility corrections are retained.

## Executable regression reproduction

`SDLWindowStyleDispatchTest` uses the actual SDL `sendCommand`, Android Handler
and Looper under Robolectric (API 35), without starting JNI or a device. Test-only
setup follows [Robolectric's Gradle configuration](https://robolectric.org/getting-started/).

- Before the runtime fix, two UI tests failed: three queued fullscreen requests
  took **1524 ms**, and the renamed-UI-thread case took **1543 ms**.
- After the fix, all five cases pass. The same three-request block measured
  **0 ms at millisecond precision** in the host test.
- Native-thread tests retain the wait, verify wake-up on the same monitor used
  by `surfaceChanged`, and verify the bounded timeout without a surface callback.
- The remaining case verifies that fullscreen and title commands retain their
  queue order and are actually applied.

These timings isolate this self-blocking bug. They are **not** measurements of
complete game startup or rendering speed on a physical device.

## Dependency changes

Included the user's catalog updates unchanged: AGP `9.3.2`, Compose BOM
`2026.08.00`, Navigation Compose `2.10.0`, AppCompat `1.8.0`, Firebase BOM
`34.18.0`. Added Robolectric `4.16.1` as a test-only dependency and enabled Android
resources for local tests; it is not a runtime dependency.

## Verification

- `testDebugUnitTest`: **118 tests, 0 failures/errors/skips**, including five new
  executable SDL dispatch cases and the existing game/input/IME regressions.
- `tools/test_native_regressions.ps1`: **89 checks passed**, including the existing
  Game DB, Vulkan import-memory selector, SFO and IME checks.
- `assembleDebug`: successful arm64 APK; verified `lib/arm64-v8a/libVita3K.so`
  is present and the bundled Game DB matches the source exactly.
- `minifyReleaseWithR8` and `optimizeReleaseResources`: passed. Inspected the
  resulting release DEX: `SDLActivity.sendCommand` still contains the Looper
  comparison before the 500 ms wait. Shrinker seeds also retain the existing
  native IME, touch-dispatch/cancellation and virtual-controller entry points.
  This checks release shrinking, not assembly/installation of a release APK.
- `lintDebug`: completed, **not clean**: 64 errors / 358 warnings; the error
  count is unchanged from the baseline (64 / 363). The project still uses
  `abortOnError = false`; successful Gradle completion does not imply clean lint.
- `git diff --check`: passed.

Changes are limited to the SDL Looper guard, its regression tests/test setup,
this note and the requested dependency updates. Existing release metadata,
baseline-profile artifacts and the unrelated `IGDB/` directory are not part of
this fix.

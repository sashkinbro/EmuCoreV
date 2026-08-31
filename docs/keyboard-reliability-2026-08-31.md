# Android game keyboard reliability — 2026-08-31

## Cause found in code

`Emulator.setKeyboardActive` only logged the request; `updateNativeImeState` and `clearNativeImeState` were no-ops. SDL attempted `showSoftInput` once, without an activity-side retry after window focus/attachment. There was no Android preview or fallback if a game relied on the Vita text service instead of drawing its own keyboard. No specific failing game was supplied, so these are code-level findings, not a reproduced device incident.

## Implementation

- The exact JNI callback ABI is retained. Snapshots for both `sceIme` and `sceImeDialog` now update UI state on the Android main thread.
- Bounded keyboard retries run at 0/200/650 ms, gated by window focus and an invalidatable request generation. Closed dialogs and background/destroyed activities cannot trigger a delayed reopen. A zero-size SDL editor is laid out again, and each new request resets the Android input connection.
- If Android reports no visible IME after 1.1 seconds, show an in-activity fallback. The user can also switch manually between Android and built-in keyboards. This follows Android's [focus/visibility guidance](https://developer.android.com/develop/ui/views/touch-and-input/keyboard-input/visibility).
- The fallback has Latin letters, numbers, common punctuation, case toggle, space, delete, caret controls, multiline newline, Enter and Cancel. Android's installed keyboards remain available for other scripts. The native text snapshot is the source of truth; no competing editable copy is maintained.
- The overlay uses the existing game window, not a new Dialog that could pause SDL. On-screen controller and gyro input are suspended while typing. The guest keeps running to consume IME callbacks. Native non-cancelable dialogs remain non-cancelable.
- Android editor actions are routed to the Vita service. Enter is queued behind pending SDL text events so the final character is not lost. Native hardware Enter/Escape close the keyboard only when appropriate.
- IME insertion/preedit enforce the guest length limit without unsigned underflow; caret movement, backspace and length truncation preserve UTF-16 surrogate pairs. Fixed the internal UTF-16 allocation's byte count. Multiline text is retained where the guest allows it.

## Verification

Host tests compile the actual native SFO and IME sources and FIFA classifier. Kotlin tests cover both IME services, preview bounds, fallback characters, lifecycle retry guards, JNI methods, Enter ordering and all 12 locale string sets. Full Android arm64 assembly and lint are also run.

- `testDebugUnitTest`: 103 tests passed, zero failures/errors.
- `tools/test_native_regressions.ps1`: 69 checks passed against the actual C++ implementations.
- `assembleDebug`: arm64 APK built successfully, version `0.2.0` / `71`; new JNI exports are present in `libVita3K.so`.
- `lintDebug`: completed, with the same 64 existing errors and 364 warnings. No lint errors were reported in the new keyboard implementation. The repository's `abortOnError = false` means a successful lint task is not a clean lint report.

No tablet, ADB or real-game testing was performed. Actual keyboard visibility on vendor Android firmware, CJK composition, controller interactions and gameplay still require a free test device. The built-in fallback is deliberately small, not a replacement for every Android keyboard language/layout.

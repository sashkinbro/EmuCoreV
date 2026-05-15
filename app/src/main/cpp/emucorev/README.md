# EmuCoreV Vita3K Native Layers

The native side is split into three layers:

1. `../vita3k` is the vendored Vita3K upstream tree. Treat it as read-only.
2. `emucorev` adapter code translates API/build differences and filters stock
   Vita3K UI targets from the Android library graph.
3. Android/JNI bridge code exposes EmuCoreV Java/Kotlin entry points.

Gradle still enters the vanilla Vita3K CMake project directly:

```text
app/src/main/cpp/vita3k/CMakeLists.txt
```

The EmuCoreV layer is attached through `CMAKE_PROJECT_Vita3K_INCLUDE`, which
loads `cmake/Vita3KProjectHook.cmake` and then `cmake/AttachToVita3K.cmake`
after the upstream `vita3k` target exists. This keeps Layer 1 replaceable.

Future Vita3K updates should replace only `app/src/main/cpp/vita3k`. The
updater must never overwrite this `emucorev` folder.

Update flow:

1. Run the local updater from the repository root:

   ```powershell
   powershell -ExecutionPolicy Bypass -File .\tools\update_vita3k_core.ps1 -Fetch
   powershell -ExecutionPolicy Bypass -File .\tools\update_vita3k_core.ps1 -Fetch -Apply
   ```

2. Build and verify:

   ```powershell
   $env:ANDROID_NDK_HOME="C:/Users/sasha/AppData/Local/Android/Sdk/ndk/29.0.14206865"
   $env:VCPKG_ROOT="C:/Users/sasha/AndroidStudioProjects/EmuCoreV/third_party/vcpkg"
   .\gradlew :app:externalNativeBuildDebug
   .\gradlew :app:assembleDebug
   ```

   Recent Vita3K Android CMake scripts read `ANDROID_NDK_HOME` and
   `VCPKG_ROOT` from the environment before Gradle enters the EmuCoreV hook.
   Keep the paths with forward slashes so CMake does not parse Windows
   backslashes as escape sequences.

3. If a Vita3K API/build contract changes, patch only `emucorev` or the Android
   bridge layer. Do not patch `../vita3k` for local integration problems.

The updater and its manifest are local-only tooling and are ignored by Git.

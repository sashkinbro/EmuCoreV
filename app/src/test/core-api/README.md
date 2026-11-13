# Android shared-core regression tests

This executable calls the real exported guest APIs in the CMake-built core shared library (`libECV.so`; legacy `libVita3K.so` is accepted by the runner). It is excluded from normal APK builds and adds no test JNI entry points to the app.

Configure the debug native build with `gradlew.bat :app:configureCMakeDebug`, then build the `emucorev-core-tests` target in the generated `app/.cxx/Debug/<configuration>/arm64-v8a` directory using the Android SDK CMake. Use the same `ANDROID_NDK_HOME` and `VCPKG_ROOT` as the app build.

Run on an explicitly selected ARM64 device:

```powershell
app/src/test/core-api/run-android-tests.ps1 -Serial DEVICE_SERIAL
```

Specify `-NativeDirectory` if multiple debug configurations contain the executable. The runner looks for `libECV.so` first and falls back to `libVita3K.so`. `-Filter` accepts a GoogleTest filter; `-CoreLibrary` can select an ABI-compatible baseline library to reproduce regressions. The selected library must keep its SONAME filename so it matches the test executable's `DT_NEEDED`; older libraries may not contain newly introduced symbols.

The runner uses a unique shell-only directory, removes its two uploaded files afterward, and does not install an APK or access game/save storage. Tests construct an uninitialized emulator environment; they cover API behavior and structure layout without starting guest CPU execution or rendering a game.

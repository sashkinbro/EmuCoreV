# Android shared-core regression tests

This executable calls the real exported guest APIs in `libVita3K.so`. It is excluded from normal APK builds and adds no test JNI entry points to the app.

Configure the debug native build with `gradlew.bat :app:configureCMakeDebug`, then build the `emucorev-core-tests` target in the generated `app/.cxx/Debug/<configuration>/arm64-v8a` directory using the Android SDK CMake. Use the same `ANDROID_NDK_HOME` and `VCPKG_ROOT` as the app build.

Run on an explicitly selected ARM64 device:

```powershell
app/src/test/core-api/run-android-tests.ps1 -Serial DEVICE_SERIAL
```

Specify `-NativeDirectory` if multiple debug configurations contain the executable. `-Filter` accepts a GoogleTest filter; `-CoreLibrary` can select an ABI-compatible baseline library to reproduce regressions. Older libraries may not contain newly introduced symbols.

The runner uses a unique shell-only directory, removes its two uploaded files afterward, and does not install an APK or access game/save storage. Tests construct an uninitialized emulator environment; they cover API behavior and structure layout without starting guest CPU execution or rendering a game.

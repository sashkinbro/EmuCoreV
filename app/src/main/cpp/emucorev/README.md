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


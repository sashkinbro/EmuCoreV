# Native code looks these up by name (GetMethodID) and binds JNI by symbol.
-keepclassmembers class com.sbro.emucorev.nativeui.shell.NativeShellActivity {
    public void onNative*(...);
    native <methods>;
}
-keepclassmembers class * extends com.sbro.emucorev.nativeui.shell.NativeShellActivity {
    public void onNative*(...);
}

# Firmware pack extractor entry points (bound by symbol).
-keepclassmembers class com.sbro.emucorev.nativeui.FirmwarePack {
    native <methods>;
}

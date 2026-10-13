# Preserve JNI/reflection names and all retained members while allowing body
# optimization, including removal of release Log calls. No allowshrinking or
# allowobfuscation is used for these externally addressed entry points.
-keepclasseswithmembernames,allowoptimization,includedescriptorclasses class * {
    native <methods>;
}

-keep,allowoptimization @androidx.annotation.Keep class * { *; }
-keepclasseswithmembers,allowoptimization class * {
    @androidx.annotation.Keep <methods>;
}

# Vita bridge, provider, overlay and SDL wrappers are all used either by
# manifest reflection, native callbacks, or SDL's own runtime lookup.
# VitaInstallBridge.onNativeProgress is invoked from JNI via GetStaticMethodID,
# so the whole class must survive shrinking.
-keep,allowoptimization,includedescriptorclasses class com.sbro.emucorev.core.NativeLib { *; }
-keep,allowoptimization,includedescriptorclasses class com.sbro.emucorev.core.VitaInstallBridge { *; }
-keep,allowoptimization,includedescriptorclasses class com.sbro.emucorev.core.vita.Emulator { *; }
-keep,allowoptimization,includedescriptorclasses class com.sbro.emucorev.core.vita.EmuSurface { *; }
-keep,allowoptimization,includedescriptorclasses class com.sbro.emucorev.core.vita.provider.VitaDocumentsProvider { *; }
-keep,allowoptimization,includedescriptorclasses class com.sbro.emucorev.core.vita.overlay.** { *; }
# The cheat bridge resolves its native entry points by JNI symbol name, and the
# cheat manager ViewModel is created reflectively by the ViewModel factory.
-keep,allowoptimization,includedescriptorclasses class com.sbro.emucorev.core.CheatBridge { *; }
-keep,allowoptimization,includedescriptorclasses class com.sbro.emucorev.ui.cheats.CheatManagerViewModel { *; }
# Save-state JNI bridge resolves its entry points by symbol name.
-keep,allowoptimization,includedescriptorclasses class com.sbro.emucorev.core.SaveStateBridge { *; }
# These explicitly retained JNI classes intentionally use no @Keep annotation:
# AndroidX/Play Services consumer @Keep rules prohibit method-body optimization.
-keep,allowoptimization,includedescriptorclasses class com.sbro.emucorev.ui.savestates.SaveStatesViewModel { *; }
# Bundled SDL/HID classes live in org.libsdl.app to match SDL's own JNI
# expectations. SDL_android.c looks them up by FQN via FindClass at JNI_OnLoad
# and SDL3 callbacks resolve their static methods by name through reflection.
-keep,allowoptimization,includedescriptorclasses class org.libsdl.app.** { *; }

# Preserve app components referenced by manifest/shortcuts/providers.
-keep,allowoptimization,includedescriptorclasses class com.sbro.emucorev.MainActivity { *; }
-keep,allowoptimization,includedescriptorclasses class com.sbro.emucorev.EmuCoreVApp { *; }
-keep,allowoptimization,includedescriptorclasses class androidx.core.content.FileProvider { *; }

# SDL.java loads ReLinker through reflection, so those names must stay stable
# once release shrinking/obfuscation is enabled.
-keep,allowoptimization,includedescriptorclasses class com.getkeepsafe.relinker.** { *; }

# Emulator restarts itself through ProcessPhoenix.
-keep,allowoptimization,includedescriptorclasses class com.jakewharton.processphoenix.** { *; }

# The Discord bridge resolves JNI symbols by class name, and the SDK is used
# reflectively from the isolated :discord process.
-keep,allowoptimization,includedescriptorclasses class com.sbro.emucorev.discord.DiscordNative { *; }
-keep,allowoptimization,includedescriptorclasses class com.discord.socialsdk.** { *; }

# Keep Kotlin metadata and annotations that Compose / reflection-adjacent code
# may rely on when stack traces or external libraries inspect them.
-keepattributes *Annotation*, InnerClasses, Signature, EnclosingMethod

# Apache Commons Compress references optional XZ/Zstd integrations. The app
# does not bundle those codecs, and R8 only needs to suppress the optional refs.
-dontwarn com.github.luben.zstd.ZstdInputStream
-dontwarn org.tukaani.xz.MemoryLimitException
-dontwarn org.tukaani.xz.SingleXZInputStream
-dontwarn org.tukaani.xz.XZInputStream

# Release has no Android logcat output. Preserve helpers that return strings or
# control results; only the logging sink calls themselves have no side effects.
-assumenosideeffects class android.util.Log {
    public static *** v(...);
    public static *** d(...);
    public static *** i(...);
    public static *** w(...);
    public static *** e(...);
    public static *** wtf(...);
    public static *** println(...);
}

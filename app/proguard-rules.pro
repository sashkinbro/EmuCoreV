# Keep JNI bridge entry points and classes touched from native code.
-keepclasseswithmembernames class * {
    native <methods>;
}

-keep @androidx.annotation.Keep class * { *; }
-keepclasseswithmembers class * {
    @androidx.annotation.Keep <methods>;
}

# Vita bridge, provider, overlay and SDL wrappers are all used either by
# manifest reflection, native callbacks, or SDL's own runtime lookup.
# VitaInstallBridge.onNativeProgress is invoked from JNI via GetStaticMethodID,
# so the whole class must survive shrinking.
-keep class com.sbro.emucorev.core.VitaInstallBridge { *; }
-keep class com.sbro.emucorev.core.vita.Emulator { *; }
-keep class com.sbro.emucorev.core.vita.EmuSurface { *; }
-keep class com.sbro.emucorev.core.vita.provider.VitaDocumentsProvider { *; }
-keep class com.sbro.emucorev.core.vita.overlay.** { *; }
# Bundled SDL/HID classes live in org.libsdl.app to match SDL's own JNI
# expectations. SDL_android.c looks them up by FQN via FindClass at JNI_OnLoad
# and SDL3 callbacks resolve their static methods by name through reflection.
-keep class org.libsdl.app.** { *; }

# Preserve app components referenced by manifest/shortcuts/providers.
-keep class com.sbro.emucorev.MainActivity { *; }
-keep class com.sbro.emucorev.EmuCoreVApp { *; }
-keep class androidx.core.content.FileProvider { *; }

# SDL.java loads ReLinker through reflection, so those names must stay stable
# once release shrinking/obfuscation is enabled.
-keep class com.getkeepsafe.relinker.** { *; }

# Emulator restarts itself through ProcessPhoenix.
-keep class com.jakewharton.processphoenix.** { *; }

# Keep Kotlin metadata and annotations that Compose / reflection-adjacent code
# may rely on when stack traces or external libraries inspect them.
-keepattributes *Annotation*, InnerClasses, Signature, EnclosingMethod

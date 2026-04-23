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
-keep class com.sbro.emucorev.core.VitaInstallBridge { *; }
-keep class com.sbro.emucorev.core.vita.Emulator { *; }
-keep class com.sbro.emucorev.core.vita.EmuSurface { *; }
-keep class com.sbro.emucorev.core.vita.provider.VitaDocumentsProvider { *; }
-keep class com.sbro.emucorev.core.vita.overlay.** { *; }
-keep class com.sbro.emucorev.core.sdl.** { *; }

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

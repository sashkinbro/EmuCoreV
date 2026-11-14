package org.vita3k.emulator

import android.app.Application
import org.libsdl.app.SDLActivity
import org.vita3k.emulator.data.UiLanguages

class Vita3KApplication : Application() {
    override fun onCreate() {
        super.onCreate()
        // Both Android entry points build the ECV target; let the linker resolve APK-backed libraries.
        System.loadLibrary("ECV")
        SDLActivity.nativeSetenv("SDL_ANDROID_ALLOW_RECREATE_ACTIVITY", "1")
        UiLanguages.applyStored(this)
    }
}

package com.sbro.emucorev.core

import android.content.Context
import com.sbro.emucorev.core.sdl.SDL

object NativeLibraryLoader {
    @Volatile
    private var loaded = false

    fun ensureLoaded(context: Context) {
        if (loaded) return
        synchronized(this) {
            if (loaded) return
            val appContext = context.applicationContext
            SDL.setContext(appContext)
            SDL.loadLibrary("Vita3K", appContext)
            loaded = true
        }
    }
}

package com.sbro.emucorev.core

import android.content.Context
import android.util.Log
import org.libsdl.app.SDL

object NativeLibraryLoader {
    private const val TAG = "NativeLibraryLoader"

    @Volatile
    private var loaded = false

    @Volatile
    private var initialized = false

    fun isNativeSessionInitialized(): Boolean {
        return loaded && runCatching { NativeLib.isInitialized() }.getOrDefault(false)
    }

    fun ensureLoaded(context: Context) {
        val appContext = context.applicationContext
        if (!loaded) {
            synchronized(this) {
                if (!loaded) {
                    SDL.setContext(appContext)
                    SDL.loadLibrary("Vita3K", appContext)
                    loaded = true
                }
            }
        }
        if (!initialized) {
            synchronized(this) {
                if (!initialized) {
                    EmulatorStorage.prepareRuntime(appContext)
                    if (!NativeLib.prepareFrontend()) {
                        Log.e(TAG, "NativeLib.prepareFrontend() failed")
                    }
                    if (!NativeLib.isInitialized()) {
                        val storagePath = EmulatorStorage.storageRoot(appContext).absolutePath
                        if (!NativeLib.init(storagePath)) {
                            Log.e(TAG, "NativeLib.init('$storagePath') failed")
                            return@synchronized
                        }
                    }
                    initialized = true
                }
            }
        }
    }
}

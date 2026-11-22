package com.sbro.emucorev.core

import android.content.Context
import android.util.Log
import com.sbro.emucorev.BuildConfig
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
                    // SDL may already have a live surface. Keep its Activity
                    // available while native initialization posts UI messages.
                    SDL.setContext(if (context is org.libsdl.app.SDLActivity) context else appContext)
                    SDL.loadLibrary("ECV", appContext)
                    loaded = true
                    // Seed SDL's cached JNI globals (activity class, getContext,
                    // AssetManager) before any native call. Native init scans the
                    // app list and reads files via SDL IO; a missing file falls
                    // back to the asset path, which aborts in JniAbort when
                    // SDLActivity.onCreate (the normal setupJNI site) hasn't run
                    // yet (app pre-warm, library/install flows from MainActivity).
                    // With an app context the fallback fails gracefully instead.
                    // SDLActivity re-runs setupJNI on creation as usual.
                    runCatching { SDL.setupJNI() }
                        .onFailure { Log.w(TAG, "SDL.setupJNI() pre-warm failed", it) }
                }
            }
        }
        if (!initialized) {
            synchronized(this) {
                if (!initialized) {
                    AppStartup.awaitReady()
                    if (EmulatorStorage.isStorageUnavailable(appContext)) {
                        // Do not let the core create a Vita tree on a fallback root; a later call
                        // retries once the selected storage is back.
                        Log.w(TAG, "Selected storage is unavailable; native init deferred")
                        return@synchronized
                    }
                    EmulatorStorage.prepareRuntime(appContext)
                    VitaCoreConfigRepository(appContext).ensureDefaultsPersisted()
                    if (!NativeLib.prepareFrontend()) {
                        Log.e(TAG, "NativeLib.prepareFrontend() failed")
                    }
                    if (!NativeLib.isInitialized()) {
                        val runtimePath = EmulatorStorage.runtimeRoot(appContext).absolutePath
                        val vitaPath = EmulatorStorage.vitaRoot(appContext).absolutePath
                        if (!NativeLib.init(runtimePath, vitaPath)) {
                            Log.e(TAG, "NativeLib.init(runtime='$runtimePath', vita='$vitaPath') failed")
                            return@synchronized
                        }
                    }
                    initialized = true
                }
            }
        }
        // Silence verbose upstream spdlog (flush_on trace) in release. Debug
        // keeps full logs. Cheap + idempotent, safe to re-apply on every call.
        if (!BuildConfig.DEBUG) {
            runCatching { NativeLib.applyReleaseLogging() }
        }
    }
}

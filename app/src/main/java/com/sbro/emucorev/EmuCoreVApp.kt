package com.sbro.emucorev

import android.app.Application
import com.sbro.emucorev.core.AndroidDiagnostics
import com.sbro.emucorev.core.NativeLib
import com.sbro.emucorev.core.AppIconManager
import com.sbro.emucorev.core.EmulatorStorage
import com.sbro.emucorev.core.NativeLibraryLoader
import com.sbro.emucorev.core.VitaCoreConfigRepository
import com.sbro.emucorev.data.AppPreferences

class EmuCoreVApp : Application() {
    override fun onCreate() {
        super.onCreate()
        AndroidDiagnostics.initialize(this)
        AppIconManager.applyProIcon(this, AppPreferences(this).proUnlocked)
        runCatching {
            EmulatorStorage.prepareRuntime(this)
            VitaCoreConfigRepository(this).ensureDefaultsPersisted()
        }
        if (AppPreferences(this).onboardingCompleted) {
            // Native init (Vulkan enumeration, app-list scan, compat DB) blocks for
            // seconds on low-end devices. Running it on the UI thread stalls
            // Choreographer/HWUI (syncAndDrawFrame) and trips the Vitals ANR
            // grouped under condition_variable::wait. Pre-warm off-thread;
            // ensureLoaded() is synchronized/idempotent so IO-thread callers
            // (launch/install) safely join the same init.
            val app = this
            Thread({
                runCatching { NativeLibraryLoader.ensureLoaded(app) }
            }, "EmuCoreV-Init").apply { isDaemon = true; start() }
        }
    }

    override fun onTrimMemory(level: Int) {
        super.onTrimMemory(level)
        if (NativeLibraryLoader.isNativeSessionInitialized()) {
            NativeLib.onTrimMemory(level)
        }
    }

    override fun onLowMemory() {
        super.onLowMemory()
        if (NativeLibraryLoader.isNativeSessionInitialized()) {
            NativeLib.onTrimMemory(80)
        }
    }
}

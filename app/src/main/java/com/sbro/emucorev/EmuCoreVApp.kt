package com.sbro.emucorev

import android.app.Application
import com.sbro.emucorev.core.AppIconManager
import com.sbro.emucorev.core.EmulatorStorage
import com.sbro.emucorev.core.NativeLibraryLoader
import com.sbro.emucorev.core.VitaCoreConfigRepository
import com.sbro.emucorev.data.AppPreferences

class EmuCoreVApp : Application() {
    override fun onCreate() {
        super.onCreate()
        AppIconManager.applyProIcon(this, AppPreferences(this).proUnlocked)
        runCatching {
            EmulatorStorage.prepareRuntime(this)
            VitaCoreConfigRepository(this).ensureDefaultsPersisted()
        }
        if (AppPreferences(this).onboardingCompleted) {
            NativeLibraryLoader.ensureLoaded(this)
        }
    }
}

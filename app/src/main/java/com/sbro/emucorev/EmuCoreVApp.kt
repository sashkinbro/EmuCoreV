package com.sbro.emucorev

import android.app.Application
import com.sbro.emucorev.core.EmulatorStorage
import com.sbro.emucorev.core.NativeLibraryLoader
import com.sbro.emucorev.core.VitaCoreConfigRepository

class EmuCoreVApp : Application() {
    override fun onCreate() {
        super.onCreate()
        runCatching {
            EmulatorStorage.prepareRuntime(this)
            VitaCoreConfigRepository(this).ensureDefaultsPersisted()
        }
        NativeLibraryLoader.ensureLoaded(this)
    }
}

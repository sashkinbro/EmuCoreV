package com.sbro.emucorev.core

object NativeLib {
    external fun prepareFrontend(): Boolean
    external fun init(storagePath: String): Boolean
    external fun isInitialized(): Boolean
    external fun refreshAppsList()
}

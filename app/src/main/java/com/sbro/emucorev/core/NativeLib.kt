package com.sbro.emucorev.core

object NativeLib {
    external fun prepareFrontend(): Boolean
    external fun init(runtimePath: String, vitaPath: String): Boolean
    external fun isInitialized(): Boolean
    external fun refreshAppsList()
}

package com.sbro.emucorev.core

object NativeLib {
    external fun onTrimMemory(level: Int)
    external fun prepareFrontend(): Boolean
    external fun init(runtimePath: String, vitaPath: String): Boolean
    external fun isInitialized(): Boolean
    external fun refreshAppsList()
    external fun setControlsEditorActive(active: Boolean)
    external fun setKeyboardMousePadState(
        padIndex: Int,
        buttons: Int,
        leftX: Float,
        leftY: Float,
        rightX: Float,
        rightY: Float
    )
    /** Silence verbose upstream spdlog in release (see native_lib_bridge.cpp). */
    external fun applyReleaseLogging()
}

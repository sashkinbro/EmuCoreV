package com.sbro.emucorev.data

/** Narrow JNI bridge for applying a validated local preset to the active Vulkan renderer. */
internal object NativeRetroArchShaderBridge {
    @JvmStatic
    external fun isSupported(): Boolean
    @JvmStatic
    external fun lastError(): String?
    @JvmStatic
    external fun setPreset(packRoot: String, presetPath: String): Boolean
    @JvmStatic
    external fun setParameter(name: String, value: Float): Boolean
    @JvmStatic
    external fun resetParameters(): Boolean
}

package com.sbro.emucorev.core.vita

import org.junit.Test

class EmulatorImeContractTest {
    @Test
    fun activityExposesExactCallbacksExpectedByNativeImeBridge() {
        val activityClass = Emulator::class.java

        activityClass.getDeclaredMethod(
            "setKeyboardActive",
            Boolean::class.javaPrimitiveType
        )
        activityClass.getDeclaredMethod(
            "updateNativeImeState",
            Boolean::class.javaPrimitiveType,
            Boolean::class.javaPrimitiveType,
            String::class.java,
            Int::class.javaPrimitiveType,
            Int::class.javaPrimitiveType,
            Int::class.javaPrimitiveType,
            Boolean::class.javaPrimitiveType,
            String::class.java
        )
        activityClass.getDeclaredMethod("clearNativeImeState")
    }
}

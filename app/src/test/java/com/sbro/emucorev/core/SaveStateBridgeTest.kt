package com.sbro.emucorev.core

import org.junit.Assert.assertEquals
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config

@RunWith(RobolectricTestRunner::class)
@Config(sdk = [35])
class SaveStateBridgeTest {
    private fun parse(raw: String): SaveStateResult {
        val method = SaveStateBridge::class.java.getDeclaredMethod("parse", String::class.java)
        method.isAccessible = true
        return method.invoke(SaveStateBridge, raw) as SaveStateResult
    }

    @Test
    fun fatalLoadCannotBeTreatedAsUsableSession() {
        val result = parse("""{"status":"internal-error","sessionUsable":false}""")
        assertEquals(false, result.sessionUsable)
        assertEquals(SaveStateResult.STATUS_INTERNAL_ERROR, result.status)
    }

    @Test
    fun failedPreflightPreservesUsableSession() {
        val result = parse("""{"status":"invalid-file","sessionUsable":true}""")
        assertEquals(true, result.sessionUsable)
        assertEquals(SaveStateResult.STATUS_INVALID_FILE, result.status)
    }

    @Test
    fun olderNativeResponseDefaultsToUsableSession() {
        assertEquals(true, parse("""{"status":"ok"}""").sessionUsable)
    }

    @Test
    fun incompatibleRendererRequestsSafeRestartOnlyAfterUsablePreflight() {
        assertEquals(true, parse("""{"status":"configuration-mismatch","sessionUsable":true}""").requiresRendererRestart)
        assertEquals(false, parse("""{"status":"configuration-mismatch","sessionUsable":false}""").requiresRendererRestart)
        assertEquals(false, parse("""{"status":"unsupported-version","sessionUsable":true}""").requiresRendererRestart)
        assertEquals(false, parse("""{"status":"invalid-file","sessionUsable":true}""").requiresRendererRestart)
    }
}

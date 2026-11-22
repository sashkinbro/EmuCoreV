package com.sbro.emucorev.core.vita

import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class RecordAudioPermissionGateTest {
    @Test
    fun asksOnlyOncePerSession() {
        val gate = RecordAudioPermissionGate()
        assertTrue(gate.shouldRequest(granted = false))
        assertFalse(gate.shouldRequest(granted = false))
    }

    @Test
    fun neverAsksWhenAlreadyGranted() {
        val gate = RecordAudioPermissionGate()
        assertFalse(gate.shouldRequest(granted = true))
        assertTrue(gate.shouldRequest(granted = false))
    }

    @Test
    fun reopensOnlyAfterGrantOfOurRequest() {
        val gate = RecordAudioPermissionGate()
        assertTrue(gate.shouldReopenCapture(RecordAudioPermissionGate.REQUEST_CODE, true))
        assertFalse(gate.shouldReopenCapture(RecordAudioPermissionGate.REQUEST_CODE, false))
        assertFalse(gate.shouldReopenCapture(1, true))
    }
}

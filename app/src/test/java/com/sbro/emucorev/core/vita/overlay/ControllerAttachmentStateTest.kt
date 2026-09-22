package com.sbro.emucorev.core.vita.overlay

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class ControllerAttachmentStateTest {
    @Test
    fun repeatedVisibleSynchronizationsDoNotReattachController() {
        val state = ControllerAttachmentState()
        var attachCalls = 0
        var detachCalls = 0

        repeat(8) {
            state.synchronize(
                shouldAttach = true,
                attach = {
                    attachCalls++
                    true
                },
                detach = { detachCalls++ }
            )
        }

        assertTrue(state.isAttached)
        assertEquals(1, attachCalls)
        assertEquals(0, detachCalls)
    }

    @Test
    fun controllerDetachesExactlyOnceWhenOverlayIsHidden() {
        val state = ControllerAttachmentState()
        var attachCalls = 0
        var detachCalls = 0

        state.synchronize(true, { attachCalls++; true }, { detachCalls++ })
        repeat(4) {
            state.synchronize(false, { attachCalls++; true }, { detachCalls++ })
        }

        assertFalse(state.isAttached)
        assertEquals(1, attachCalls)
        assertEquals(1, detachCalls)
    }

    @Test
    fun failedEarlyAttachIsRetriedUntilNativeControllerIsReady() {
        val state = ControllerAttachmentState()
        var attachCalls = 0

        repeat(3) {
            state.synchronize(
                shouldAttach = true,
                attach = {
                    attachCalls++
                    attachCalls >= 3
                },
                detach = {}
            )
        }
        repeat(4) {
            state.synchronize(true, { attachCalls++; true }, {})
        }

        assertTrue(state.isAttached)
        assertEquals(3, attachCalls)
    }

    @Test
    fun forgettingADroppedNativeControllerAllowsReattach() {
        val state = ControllerAttachmentState()
        var attachCalls = 0
        var detachCalls = 0

        state.synchronize(true, { attachCalls++; true }, { detachCalls++ })
        assertTrue(state.isAttached)

        // The native session dropped the virtual controller behind our back (relaunch).
        state.forgetAttachment()
        state.synchronize(true, { attachCalls++; true }, { detachCalls++ })

        assertTrue(state.isAttached)
        assertEquals(2, attachCalls)
        assertEquals(0, detachCalls)
    }
}

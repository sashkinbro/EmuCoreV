package com.sbro.emucorev.ui.emulation

import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class TouchPointerOwnerTest {
    @Test
    fun secondPointerCannotReleaseFirstPointersControl() {
        val owner = TouchPointerOwner()

        assertTrue(owner.acquire(11))
        assertTrue(owner.acquire(11))
        assertFalse(owner.acquire(27))
        assertFalse(owner.release(27))
        assertTrue(owner.hasOwner)
        assertTrue(owner.release(11))
        assertFalse(owner.hasOwner)
    }

    @Test
    fun cancelReleasesTheCurrentPointerExactlyOnce() {
        val owner = TouchPointerOwner()

        assertFalse(owner.cancel())
        assertTrue(owner.acquire(4))
        assertTrue(owner.cancel())
        assertFalse(owner.hasOwner)
        assertFalse(owner.cancel())
        assertTrue(owner.acquire(9))
    }
}

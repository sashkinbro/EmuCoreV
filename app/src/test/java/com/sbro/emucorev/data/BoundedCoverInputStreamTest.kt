package com.sbro.emucorev.data

import java.io.ByteArrayInputStream
import org.junit.Assert.*
import org.junit.Test

class BoundedCoverInputStreamTest {
    private fun stream(bytes: Int = 5) = BoundedCoverInputStream(ByteArrayInputStream(ByteArray(bytes) { it.toByte() }), 4)
    @Test fun exactlyBudgetSizedImagesReachEof() {
        stream(4).use { input -> assertArrayEquals(byteArrayOf(0, 1, 2, 3), input.readBytes()) }
    }
    @Test fun singleByteReadsCannotBypassBudget() {
        stream().use { input ->
            repeat(4) { assertTrue(input.read() >= 0) }
            assertThrows(IllegalArgumentException::class.java) { input.read() }
        }
    }
    @Test fun bulkReadsCannotBypassBudget() {
        stream().use { input ->
            assertThrows(IllegalArgumentException::class.java) { input.read(ByteArray(8), 0, 8) }
        }
    }
    @Test fun skipAndReadShareOneBudget() {
        stream().use { input ->
            assertEquals(3L, input.skip(3)); assertTrue(input.read() >= 0)
            assertThrows(IllegalArgumentException::class.java) { input.skip(1) }
        }
    }
}

package com.sbro.emucorev.core

import androidx.test.platform.app.InstrumentationRegistry
import java.io.File
import java.nio.file.AtomicMoveNotSupportedException
import java.util.UUID
import java.util.concurrent.CountDownLatch
import java.util.concurrent.Executors
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicBoolean
import java.util.concurrent.atomic.AtomicInteger
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class AtomicTextFileDeviceTest {
    @Test fun readersAlwaysSeeACompleteFileDuringReplacement() = verifyReplacement(false)

    @Test fun externalStorageFallbackDoesNotUnlinkTheVisibleFile() = verifyReplacement(true)

    private fun verifyReplacement(forceFallback: Boolean) {
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        val directory = File(context.externalCacheDir, "atomic-config-test-${UUID.randomUUID()}")
        assertTrue(directory.mkdirs())
        val target = File(directory, "config.yml")
        val contents = listOf("A".repeat(16_384) + "\nEND-A\n", "B".repeat(16_384) + "\nEND-B\n")
        target.writeText(contents.first())
        val reading = AtomicBoolean(true)
        val ready = CountDownLatch(2)
        val reads = AtomicInteger()
        val executor = Executors.newFixedThreadPool(2)
        val readers = List(2) {
            executor.submit {
                ready.countDown()
                while (reading.get()) {
                    // Deliberately open by pathname each time, as another app
                    // process does. Keeping an old file descriptor hides gaps.
                    val content = target.readText()
                    check(content in contents) { "Reader observed a partial config" }
                    reads.incrementAndGet()
                }
            }
        }
        try {
            assertTrue(ready.await(5, TimeUnit.SECONDS))
            repeat(150) { index ->
                if (forceFallback) {
                    AtomicTextFile.write(target, contents[index % 2]) { from, to ->
                        throw AtomicMoveNotSupportedException(from.path, to.path, "Exercise external-storage fallback")
                    }
                } else {
                    AtomicTextFile.write(target, contents[index % 2])
                }
            }
        } finally {
            reading.set(false)
            executor.shutdown()
            try {
                readers.forEach { it.get(10, TimeUnit.SECONDS) }
                assertTrue("Concurrent readers must exercise file replacement", reads.get() > 0)
                assertEquals(contents[149 % 2], target.readText())
                assertFalse(directory.listFiles().orEmpty().any { it.name.endsWith(".tmp") })
            } finally {
                directory.deleteRecursively()
            }
        }
    }
}

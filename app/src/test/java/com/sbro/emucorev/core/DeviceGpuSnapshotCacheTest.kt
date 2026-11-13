package com.sbro.emucorev.core

import java.util.concurrent.CountDownLatch
import java.util.concurrent.Executors
import java.util.concurrent.TimeUnit
import org.junit.Assert.*
import org.junit.Test

class DeviceGpuSnapshotCacheTest {
    private val fallback = DeviceGpuInfoResolver.resolve(null, "Snapdragon 8 Gen 2")
    private val measured = DeviceGpuInfoResolver.resolve("Adreno (TM) 740", null)

    @Test fun `empty cache resolves once and publishes observable snapshot`() {
        val cache = DeviceGpuSnapshotCache()
        assertNull(cache.peek())
        assertNull(cache.snapshot.value)
        assertSame(fallback, cache.getOrResolve { fallback })
        assertSame(fallback, cache.getOrResolve { error("must use cache") })
        assertSame(fallback, cache.snapshot.value)
    }

    @Test fun `completed measurement wins over older slow fallback without blocking publication`() {
        val cache = DeviceGpuSnapshotCache()
        val entered = CountDownLatch(1)
        val release = CountDownLatch(1)
        val executor = Executors.newSingleThreadExecutor()
        try {
            val result = executor.submit<DeviceGpuSnapshot> {
                cache.getOrResolve {
                    entered.countDown()
                    check(release.await(3, TimeUnit.SECONDS))
                    fallback
                }
            }
            assertTrue(entered.await(3, TimeUnit.SECONDS))
            cache.publish(measured)
            release.countDown()
            assertSame(measured, result.get(3, TimeUnit.SECONDS))
            assertSame(measured, cache.peek())
            assertSame(measured, cache.snapshot.value)
        } finally {
            release.countDown()
            executor.shutdownNow()
        }
    }

    @Test fun `failed acquisition leaves cache available for later evidence`() {
        val cache = DeviceGpuSnapshotCache()
        assertThrows(SecurityException::class.java) { cache.getOrResolve { throw SecurityException() } }
        assertNull(cache.peek())
        cache.publish(measured)
        assertSame(measured, cache.getOrResolve { error("must use measurement") })
    }

    @Test fun `later explicit publication updates cached and observable evidence together`() {
        val cache = DeviceGpuSnapshotCache()
        cache.publish(fallback)
        cache.publish(measured)
        assertSame(measured, cache.peek())
        assertSame(measured, cache.snapshot.value)
    }
}

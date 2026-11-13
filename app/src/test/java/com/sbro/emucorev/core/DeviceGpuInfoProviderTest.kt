package com.sbro.emucorev.core

import java.util.concurrent.CountDownLatch
import java.util.concurrent.Executors
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicInteger
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.async
import kotlinx.coroutines.awaitAll
import kotlinx.coroutines.cancel
import kotlinx.coroutines.runBlocking
import org.junit.Assert.*
import org.junit.Test

class DeviceGpuInfoProviderTest {
    @Test fun getOnlyUsesBuildFallbackUntilBackgroundLoad() {
        val scope = CoroutineScope(SupervisorJob() + Dispatchers.Default)
        try {
            var acquired = 0
            var queried = 0
            val loader = DeviceGpuInfoLoader({ "Allwinner A523" }, { acquired++; "Allwinner H700" }, { queried++; "Mali-G31" }, scope)
            assertEquals("Allwinner A523", loader.cpuName())
            assertEquals("Mali-G57 MC1", loader.get().model?.displayName)
            assertEquals(GpuInfoSource.SOC_CATALOG, loader.get().source)
            assertEquals(0, acquired)
            assertEquals(0, queried)
        } finally { scope.cancel() }
    }

    @Test fun concurrentLoadsAcquireOnceOffCallerThreadAndPublishCpuAndGpu() = runBlocking {
        val scope = CoroutineScope(SupervisorJob() + Dispatchers.Default)
        val acquired = AtomicInteger()
        val queried = AtomicInteger()
        val caller = Thread.currentThread()
        try {
            val loader = DeviceGpuInfoLoader({ "Unknown SoC" }, {
                assertNotSame(caller, Thread.currentThread())
                acquired.incrementAndGet()
                "Allwinner H700"
            }, {
                assertNotSame(caller, Thread.currentThread())
                queried.incrementAndGet()
                "Mali-G31"
            }, scope)
            val results = List(12) { async { loader.ensureLoaded() } }.awaitAll()
            assertEquals(1, acquired.get())
            assertEquals(1, queried.get())
            assertTrue(results.all { it == loader.get() })
            assertEquals("Allwinner H700", loader.cpuName())
            assertEquals("Mali-G31 MP2", loader.snapshot.value?.model?.displayName)
            assertEquals(GpuInfoSource.GL_RENDERER, loader.get().source)
        } finally { scope.cancel() }
    }

    @Test fun cancellingOneWaiterDoesNotCancelSharedAcquisition() = runBlocking {
        val scope = CoroutineScope(SupervisorJob() + Dispatchers.Default)
        val entered = CountDownLatch(1)
        val release = CountDownLatch(1)
        val queries = AtomicInteger()
        try {
            val loader = DeviceGpuInfoLoader({ "Unknown SoC" }, { "Unknown SoC" }, {
                queries.incrementAndGet()
                entered.countDown()
                check(release.await(3, TimeUnit.SECONDS))
                "Adreno 750"
            }, scope)
            val first = scope.async { loader.ensureLoaded() }
            assertTrue(entered.await(3, TimeUnit.SECONDS))
            first.cancel()
            release.countDown()
            assertEquals("Adreno 750", loader.ensureLoaded().model?.displayName)
            assertEquals(1, queries.get())
        } finally { release.countDown(); scope.cancel() }
    }

    @Test fun deniedAcquisitionFallsBackOnceWithoutClaimingMeasuredGpu() = runBlocking {
        val scope = CoroutineScope(SupervisorJob() + Dispatchers.Default)
        val queries = AtomicInteger()
        try {
            val loader = DeviceGpuInfoLoader({ "Allwinner A523" }, { throw SecurityException() }, {
                queries.incrementAndGet(); throw IllegalStateException("EGL unavailable")
            }, scope)
            repeat(2) {
                assertEquals(GpuInfoSource.SOC_CATALOG, loader.ensureLoaded().source)
            }
            assertEquals(1, queries.get())
            assertEquals("Allwinner A523", loader.cpuName())
        } finally { scope.cancel() }
    }

    @Test fun slowBuildFallbackCannotOverwriteCompletedProbe() = runBlocking {
        val scope = CoroutineScope(SupervisorJob() + Dispatchers.Default)
        val executor = Executors.newSingleThreadExecutor()
        val entered = CountDownLatch(1)
        val release = CountDownLatch(1)
        try {
            val loader = DeviceGpuInfoLoader({
                entered.countDown(); check(release.await(3, TimeUnit.SECONDS)); "Allwinner A523"
            }, { "Allwinner H700" }, { "Mali-G31" }, scope)
            val fallback = executor.submit<DeviceGpuSnapshot> { loader.get() }
            assertTrue(entered.await(3, TimeUnit.SECONDS))
            val measured = loader.ensureLoaded()
            release.countDown()
            assertEquals(measured, fallback.get(3, TimeUnit.SECONDS))
            assertEquals(measured, loader.snapshot.value)
            assertEquals("Allwinner H700", loader.cpuName())
        } finally { release.countDown(); executor.shutdownNow(); scope.cancel() }
    }

    @Test fun softwareProbeIsNotConvertedIntoPhysicalGlEvidence() = runBlocking {
        val scope = CoroutineScope(SupervisorJob() + Dispatchers.Default)
        try {
            val loader = DeviceGpuInfoLoader({ "Unknown SoC" }, { "Unknown SoC" }, { "SwiftShader" }, scope)
            val result = loader.ensureLoaded()
            assertNull(result.model)
            assertEquals("SwiftShader", result.renderer)
            assertEquals(GpuInfoSource.UNKNOWN, result.source)
        } finally { scope.cancel() }
    }
}

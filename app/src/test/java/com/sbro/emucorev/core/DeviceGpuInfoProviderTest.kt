package com.sbro.emucorev.core

import java.io.File
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

    private class FakePersistence(var stored: DeviceGpuSnapshot? = null) : DeviceGpuPersistence {
        var saves = 0
        override fun load() = stored
        override fun save(snapshot: DeviceGpuSnapshot) { saves++; stored = snapshot }
    }

    @Test fun persistedMeasurementSeedsColdGetAsGlRendererAndSx2IsSelectable() {
        val scope = CoroutineScope(SupervisorJob() + Dispatchers.Default)
        try {
            val persisted = DeviceGpuInfoResolver.resolve("Mali-G715-Immortalis MC11", null)
            var queried = 0
            val loader = DeviceGpuInfoLoader({ "Unknown SoC" }, { "Unknown SoC" }, { queried++; null }, scope,
                FakePersistence(persisted))
            val device = loader.get()
            assertEquals(GpuInfoSource.GL_RENDERER, device.source)
            assertTrue(loader.hasMeasuredSnapshot())
            assertEquals(0, queried)
            val sx2 = InstalledGpuDriver("sx2", "libvulkan_malisx2.so", "/unused", false, GpuDriverVendor.MALI,
                GpuDriverFamily.MALI_SX2, listOf("G715"), GpuDriverFileState.VALID)
            assertEquals("sx2", resolveGpuDriverForLaunch("sx2", "Vulkan", device, listOf(sx2)))
        } finally { scope.cancel() }
    }

    @Test fun missingPersistenceKeepsSocFallbackAndSx2StaysSystem() {
        val scope = CoroutineScope(SupervisorJob() + Dispatchers.Default)
        try {
            val loader = DeviceGpuInfoLoader({ "Allwinner A523" }, { "x" }, { null }, scope, FakePersistence(null))
            assertEquals(GpuInfoSource.SOC_CATALOG, loader.get().source)
            assertFalse(loader.hasMeasuredSnapshot())
        } finally { scope.cancel() }
    }

    @Test fun successfulProbeIsPersistedButFallbackAndSoftwareAreNot() = runBlocking {
        val scope = CoroutineScope(SupervisorJob() + Dispatchers.Default)
        try {
            val store = FakePersistence()
            DeviceGpuInfoLoader({ "Unknown SoC" }, { "Unknown SoC" }, { "Mali-G720-Immortalis MC12" }, scope, store)
                .ensureLoaded()
            assertEquals(1, store.saves)
            assertEquals("Immortalis-G720 MC12", store.stored?.model?.displayName)
            val none = FakePersistence()
            DeviceGpuInfoLoader({ "Allwinner A523" }, { "x" }, { null }, scope, none).ensureLoaded()
            DeviceGpuInfoLoader({ "Unknown SoC" }, { "Unknown SoC" }, { "SwiftShader" }, scope, none).ensureLoaded()
            assertEquals(0, none.saves)
        } finally { scope.cancel() }
    }

    @Test fun snapshotFileInvalidatesWhenBuildKeyChanges() {
        val dir = java.nio.file.Files.createTempDirectory("gpu-snap").toFile()
        try {
            var key: String? = "fp-1|vk=1"
            val file = File(dir, "gpu_probe.txt")
            val store = DeviceGpuSnapshotFile({ file }, { key })
            assertNull(store.load())
            store.save(DeviceGpuInfoResolver.resolve("Mali-G715 MC7", null))
            val loaded = store.load()
            assertEquals(GpuInfoSource.GL_RENDERER, loaded?.source)
            assertEquals("Mali-G715 MC7", loaded?.model?.displayName)
            assertEquals(GpuVendor.MALI, loaded?.model?.vendor)
            key = "fp-2|vk=1"
            assertNull(store.load())
            key = null
            assertNull(store.load())
            key = "fp-1|vk=1"
            store.save(DeviceGpuSnapshot(null, null, GpuInfoSource.SOC_CATALOG))
            assertEquals("Mali-G715 MC7", store.load()?.model?.displayName)
        } finally { dir.deleteRecursively() }
    }

    @Test fun failedProbeIsNotCachedForeverAndRetriesAfterBackoff() = runBlocking {
        val scope = CoroutineScope(SupervisorJob() + Dispatchers.Default)
        try {
            var now = 0L
            val queries = AtomicInteger()
            val loader = DeviceGpuInfoLoader({ "Allwinner A523" }, { "Allwinner A523" }, {
                if (queries.incrementAndGet() == 1) null else "Mali-G715 MC7"
            }, scope, null, retryBackoffMillis = 1_000, clockMillis = { now })
            assertEquals(GpuInfoSource.SOC_CATALOG, loader.ensureLoaded().source)
            now = 500
            assertEquals(GpuInfoSource.SOC_CATALOG, loader.ensureLoaded().source)
            assertEquals(1, queries.get())
            now = 1_500
            assertEquals(GpuInfoSource.GL_RENDERER, loader.ensureLoaded().source)
            assertEquals(2, queries.get())
            now = 10_000
            loader.ensureLoaded()
            assertEquals(2, queries.get())
        } finally { scope.cancel() }
    }

    @Test fun failedReprobeNeverDowngradesPersistedMeasurement() = runBlocking {
        val scope = CoroutineScope(SupervisorJob() + Dispatchers.Default)
        try {
            val persisted = DeviceGpuInfoResolver.resolve("Mali-G715 MC7", null)
            val loader = DeviceGpuInfoLoader({ "Allwinner A523" }, { "x" }, { null }, scope, FakePersistence(persisted))
            loader.get()
            assertEquals(GpuInfoSource.GL_RENDERER, loader.ensureLoaded().source)
            assertEquals(GpuInfoSource.GL_RENDERER, loader.get().source)
        } finally { scope.cancel() }
    }
}

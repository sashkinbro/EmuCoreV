package com.sbro.emucorev.core

import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import org.junit.Assert.*
import org.junit.Test

class GpuRendererQueryTest {
    private class Backend(private val fail: String? = null, private val failCleanup: Boolean = false) : GpuRendererProbeBackend {
        val calls = mutableListOf<String>()
        private fun step(name: String): Boolean { calls += name; return fail != name }
        override fun initialize() = step("initialize")
        override fun createContext() = step("context")
        override fun createSurface() = step("surface")
        override fun makeCurrent() = step("current")
        override fun renderer(): String { calls += "renderer"; if (fail == "renderer") error("driver error"); return "Mali-G57" }
        override fun clearCurrent() { calls += "clear"; if (failCleanup) error("cleanup error") }
        override fun destroySurface() { calls += "destroySurface" }
        override fun destroyContext() { calls += "destroyContext" }
        override fun terminate() { calls += "terminate" }
        override fun releaseThread() { calls += "releaseThread" }
    }

    @Test fun everyFailureStillAttemptsAllCleanupStepsIndependently() {
        for (stage in listOf("initialize", "context", "surface", "current", "renderer")) {
            val backend = Backend(stage, failCleanup = true)
            assertNull(stage, queryGpuRendererSession(backend))
            assertEquals(stage, listOf("clear", "destroySurface", "destroyContext", "terminate", "releaseThread"), backend.calls.takeLast(5))
            if (stage != "renderer") assertFalse(backend.calls.contains("renderer"))
        }
    }

    @Test fun successfulProbeReadsBeforeReleasingItsContext() {
        val backend = Backend()
        assertEquals("Mali-G57", queryGpuRendererSession(backend))
        assertEquals(listOf("initialize", "context", "surface", "current", "renderer", "clear", "destroySurface", "destroyContext", "terminate", "releaseThread"), backend.calls)
    }

    @Test fun boundedProbeRunsOnItsOwnThreadAndReturnsUnavailableOnTimeout() {
        val caller = Thread.currentThread()
        assertEquals("Adreno 750", runBoundedGpuProbe(1000) { assertNotSame(caller, Thread.currentThread()); "Adreno 750" })
        val exited = CountDownLatch(1)
        assertNull(runBoundedGpuProbe(30) {
            try { CountDownLatch(1).await(); "unreachable" } finally { exited.countDown() }
        })
        assertTrue(exited.await(2, TimeUnit.SECONDS))
    }
}

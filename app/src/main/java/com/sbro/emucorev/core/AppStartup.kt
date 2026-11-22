package com.sbro.emucorev.core

import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit

/**
 * Heavy Application.onCreate work (storage skeleton, play-time recovery, defaults persistence)
 * runs on a worker thread so it never blocks the main thread. Consumers that must observe its
 * result (native init, the Emulator activity, which starts a new play-time session) call
 * [awaitReady] first. In processes that skip app startup nothing is begun and it returns at once.
 */
object AppStartup {
    // A fresh latch per begin(): the process normally begins once, but an Application recreated in
    // the same process (instrumentation/Robolectric) must not observe the previous run as ready.
    @Volatile private var ready = CountDownLatch(1)
    @Volatile private var begun = false

    fun begin(work: () -> Unit) {
        val latch = CountDownLatch(1)
        ready = latch
        begun = true
        if (System.getProperty("emucorev.startup.sync") == "true") {
            try {
                work()
            } finally {
                latch.countDown()
            }
            return
        }
        Thread({
            try {
                work()
            } finally {
                latch.countDown()
            }
        }, "EmuCoreV-Startup").apply { isDaemon = true; start() }
    }

    fun awaitReady(timeoutMs: Long = DEFAULT_TIMEOUT_MS): Boolean {
        if (!begun) return true
        return try {
            ready.await(timeoutMs, TimeUnit.MILLISECONDS)
        } catch (_: InterruptedException) {
            Thread.currentThread().interrupt()
            false
        }
    }

    private const val DEFAULT_TIMEOUT_MS = 15_000L
}

package com.sbro.emucorev.core

import android.content.Context
import java.io.File

/**
 * Cross-process debounce for game launches. The caller (main app or the resident native shell
 * process) takes the gate before sending the launch Intent; the Emulator process releases it once
 * it has finished its launch preparation (or failed). A launch that never reports back stops
 * blocking after [timeoutMs], so a crashed start cannot wedge the library.
 */
internal class LaunchGate(
    private val marker: File,
    private val clock: () -> Long = System::currentTimeMillis,
    private val timeoutMs: Long = TIMEOUT_MS
) {
    /** Returns false while a previous launch is still pending. */
    @Synchronized
    fun tryAcquire(): Boolean {
        if (isPending()) return false
        return try {
            marker.parentFile?.mkdirs()
            marker.writeText(clock().toString())
            true
        } catch (_: Exception) {
            // An unwritable marker must not make the game unlaunchable; the in-process guard still applies.
            true
        }
    }

    @Synchronized
    fun release() {
        runCatching { marker.delete() }
    }

    @Synchronized
    fun isPending(): Boolean {
        val acquiredAt = runCatching { marker.takeIf(File::isFile)?.readText()?.trim()?.toLong() }.getOrNull()
            ?: return false
        val age = clock() - acquiredAt
        // A clock that moved backwards must not hold the gate closed.
        return age in 0 until timeoutMs
    }

    companion object {
        const val TIMEOUT_MS = 15_000L
        private const val FILE_NAME = "launch-pending"

        fun forContext(context: Context) = LaunchGate(File(context.applicationContext.cacheDir, FILE_NAME))
    }
}

/**
 * Records that a game Emulator process is alive. A fresh launch may only clear the task stack
 * (so no stale retained intent is re-delivered) when no live game would be torn down by it; a
 * live game receives the Intent through onNewIntent and shuts down gracefully instead.
 */
internal class GameSessionMarker(
    private val marker: File,
    private val processAlive: (pid: Int, processName: String) -> Boolean = ::procAlive
) {
    fun markLive(pid: Int, processName: String) {
        runCatching {
            marker.parentFile?.mkdirs()
            marker.writeText("$pid\n$processName")
        }
    }

    fun clear(pid: Int) {
        runCatching {
            val owner = marker.takeIf(File::isFile)?.readLines()?.firstOrNull()?.trim()?.toIntOrNull()
            if (owner == null || owner == pid) marker.delete()
        }
    }

    fun isLive(): Boolean {
        if (!marker.isFile) return false
        val lines = runCatching { marker.readLines() }.getOrNull() ?: return true
        val pid = lines.getOrNull(0)?.trim()?.toIntOrNull() ?: return false
        val processName = lines.getOrNull(1)?.trim().orEmpty()
        return processAlive(pid, processName)
    }

    companion object {
        private const val FILE_NAME = "game-session"

        fun forContext(context: Context) = GameSessionMarker(File(context.applicationContext.cacheDir, FILE_NAME))

        private fun procAlive(pid: Int, processName: String): Boolean {
            val cmdline = File("/proc/$pid/cmdline")
            if (!cmdline.exists()) return false
            // If the name cannot be read, assume the session is still live: skipping the
            // task clear is always safe, tearing a running game down is not.
            val name = runCatching { cmdline.readText().trim('\u0000', ' ', '\n') }.getOrNull() ?: return true
            return processName.isEmpty() || name.startsWith(processName)
        }
    }
}

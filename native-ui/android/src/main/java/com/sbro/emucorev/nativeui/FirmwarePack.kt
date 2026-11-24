package com.sbro.emucorev.nativeui

import android.content.Context
import android.util.Log
import java.io.File
import java.io.RandomAccessFile
import java.security.MessageDigest

/**
 * The shell's resource pack, built on the device from the firmware the user
 * installed (vs0 / sa0). Nothing derived from firmware is bundled in the
 * application: without installed firmware there is no pack and the native shell
 * is not offered.
 *
 * The pack lives in a versioned, app-private, non-backed-up directory. The
 * version key covers the identity of the installed firmware sources and the
 * extractor revision, so a reinstall or an extractor change rebuilds it. Builds
 * are staged and activated by an atomic rename under a process/file lock.
 */
object FirmwarePack {
    /** Mirrors kExtractorRevision of the native extractor (checked when it runs). */
    const val EXTRACTOR_REVISION = 1

    private const val TAG = "FirmwarePack"

    /** Firmware files that must exist for the pack to be buildable (mirrors firmware_present()). */
    private val SOURCES = listOf(
        "vs0/vsh/shell/topmenu_plugin.rco",
        "vs0/data/external/common/common_resource.rco",
        "vs0/data/internal/keylock/keylock.png",
        "sa0/data/font/pvf/ltn0.pvf",
        "sa0/data/font/pvf/ltn4.pvf",
    )

    /** Application-owned (not firmware) system applications copied into the pack. */
    private const val OWN_SYSTEM_APPS_ASSET_ROOT = "system-apps"

    /** True when the installed firmware has the sources the pack is built from. */
    fun sourcesPresent(vitaRoot: File): Boolean = SOURCES.all { File(vitaRoot, it).isFile }

    /** Identity of the pack for the installed firmware; null when it is not installed. */
    fun key(vitaRoot: File): String? {
        if (!sourcesPresent(vitaRoot)) return null
        val digest = MessageDigest.getInstance("SHA-256")
        digest.update("extractor=$EXTRACTOR_REVISION\n".toByteArray())
        for (relative in SOURCES) {
            val file = File(vitaRoot, relative)
            digest.update("$relative:${file.length()}:${file.lastModified()}\n".toByteArray())
        }
        File(vitaRoot, "system-packages").listFiles { f -> f.isFile && f.name.endsWith(".version") }
            .orEmpty().sortedBy { it.name }.forEach {
                digest.update("${it.name}=${runCatching { it.readText().trim() }.getOrDefault("")}\n".toByteArray())
            }
        return digest.digest().joinToString("") { "%02x".format(it) }.take(32)
    }

    /** The already built pack for the installed firmware, without extracting anything. */
    fun readyDirectory(context: Context, vitaRoot: File): File? {
        val key = key(vitaRoot) ?: return null
        val directory = File(baseDirectory(context), key)
        return directory.takeIf { File(it, ".complete").readTextOrNull() == key }
    }

    fun isReady(context: Context, vitaRoot: File): Boolean = readyDirectory(context, vitaRoot) != null

    /** Removes every built pack (firmware reinstall or removal). */
    @Synchronized
    fun invalidate(context: Context) {
        val base = baseDirectory(context)
        if (!base.isDirectory) return
        withLock(base) { base.listFiles()?.filter { it.isDirectory }?.forEach { it.deleteRecursively() } }
    }

    /**
     * Returns the pack directory for the installed firmware, building it first when
     * needed. Runs the native extractor; call off the main thread (a few seconds).
     */
    @Synchronized
    fun prepare(context: Context, vitaRoot: File): File {
        val version = checkNotNull(key(vitaRoot)) { "Firmware is not installed" }
        val base = baseDirectory(context)
        check(base.isDirectory || base.mkdirs()) { "Cannot create UI resource directory" }
        return withLock(base) {
            val destination = File(base, version)
            if (File(destination, ".complete").readTextOrNull() == version) return@withLock destination
            // A new pack cannot replace the previous ready directory until it is complete.
            val staging = File(base, "staging")
            check(!staging.exists() || staging.deleteRecursively()) { "Cannot remove partial UI pack" }
            check(staging.mkdirs()) { "Cannot stage UI pack" }
            try {
                ensureLoaded()
                check(nativeRevision() == EXTRACTOR_REVISION) { "Extractor revision mismatch" }
                nativeExtract(vitaRoot.absolutePath, staging.absolutePath)?.let { error(it) }
                copyOwnSystemApps(context, File(staging, "system-apps"))
                File(staging, ".complete").writeText(version)
                check(!destination.exists() || destination.deleteRecursively())
                check(staging.renameTo(destination)) { "Cannot activate UI resource pack" }
                base.listFiles()?.filter { it.isDirectory && it != destination }?.forEach { it.deleteRecursively() }
                destination
            } catch (error: Exception) {
                staging.deleteRecursively()
                throw error
            }
        }
    }

    /** Builds the pack when firmware is installed and no current pack exists; never throws. */
    fun prepareIfPossible(context: Context, vitaRoot: File): Boolean = try {
        sourcesPresent(vitaRoot) && prepare(context, vitaRoot).isDirectory
    } catch (error: Throwable) {
        Log.e(TAG, "Cannot build the shell resource pack from the installed firmware", error)
        false
    }

    private fun baseDirectory(context: Context) = File(context.noBackupFilesDir, "native-ui-resources")

    private inline fun <T> withLock(base: File, action: () -> T): T {
        check(base.isDirectory || base.mkdirs()) { "Cannot create UI resource directory" }
        return RandomAccessFile(File(base, "install.lock"), "rw").channel.use { channel ->
            channel.lock().use { action() }
        }
    }

    /** Own artwork of EmuCoreV's built-in applications (never firmware content). */
    private fun copyOwnSystemApps(context: Context, target: File) {
        fun copy(assetPath: String, destination: File) {
            val children = context.assets.list(assetPath).orEmpty()
            if (children.isEmpty()) {
                destination.parentFile!!.mkdirs()
                context.assets.open(assetPath).use { input -> destination.outputStream().use { input.copyTo(it) } }
            } else {
                children.forEach { copy("$assetPath/$it", File(destination, it)) }
            }
        }
        copy(OWN_SYSTEM_APPS_ASSET_ROOT, target)
    }

    private fun File.readTextOrNull(): String? = if (isFile) readText() else null

    @Volatile private var loaded = false

    @Synchronized
    private fun ensureLoaded() {
        if (!loaded) {
            System.loadLibrary("emucorev_packjni")
            loaded = true
        }
    }

    /** Returns null on success, otherwise the extractor's error text. */
    private external fun nativeExtract(vitaRoot: String, outDirectory: String): String?
    private external fun nativeRevision(): Int
}

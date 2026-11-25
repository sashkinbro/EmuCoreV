package com.sbro.emucorev.core

import java.io.File

/**
 * What "firmware installed" means on disk.
 *
 * The native installer ([VitaInstallBridge.installFirmware]) swaps whole partitions into the Vita root and writes the
 * completion marker [MARKER_NAME] last. It looks like this:
 *
 * ```
 * emucorev-firmware-marker 1
 * version=3.74
 * os0.files=69
 * vs0.files=1473
 * sa0.files=178
 * ```
 *
 * `version` is the version.txt of the last base package (fonts-only packages keep it), `<partition>.files` the number of
 * files installed for that partition. A tree is only reported as installed when the key files exist and, if a marker is
 * present, it is well formed and covers the partition. Trees installed before the marker existed have no marker; they are
 * accepted on the key files alone (they cannot be verified any further).
 */
object FirmwareInstallState {
    const val MARKER_NAME = "fw.complete"
    internal const val MARKER_HEADER = "emucorev-firmware-marker 1"

    /** Present in every complete base firmware (vs0 partition). */
    const val SHELL_KEY_FILE = "vs0/vsh/shell/shell.self"

    /** Present in the fonts/update package (sa0 partition). */
    const val FONT_KEY_FILE = "sa0/data/font/pvf/ltn0.pvf"

    data class Marker(val version: String, val fileCounts: Map<String, Long>) {
        val partitions: Set<String> get() = fileCounts.keys
    }

    /** Parses a marker; `null` when it is not a marker of this format. */
    fun parseMarker(text: String): Marker? {
        val lines = text.lineSequence().map { it.trimEnd('\r') }.toList()
        if (lines.firstOrNull() != MARKER_HEADER) return null
        var version = ""
        val counts = linkedMapOf<String, Long>()
        for (line in lines.drop(1)) {
            val eq = line.indexOf('=')
            if (eq <= 0) continue
            val key = line.substring(0, eq)
            val value = line.substring(eq + 1)
            when {
                key == "version" -> version = value
                key.endsWith(".files") -> value.toLongOrNull()?.let { counts[key.removeSuffix(".files")] = it }
            }
        }
        return Marker(version, counts)
    }

    /** The marker of an install, `null` when absent or unreadable. */
    fun readMarker(vitaRoot: File): Marker? {
        val file = File(vitaRoot, MARKER_NAME)
        if (!file.isFile) return null
        return runCatching { parseMarker(file.readText()) }.getOrNull()
    }

    private fun hasMarkerFile(vitaRoot: File) = File(vitaRoot, MARKER_NAME).exists()

    private fun keyFilePresent(vitaRoot: File, relative: String): Boolean {
        val file = File(vitaRoot, relative)
        return file.isFile && file.length() > 0
    }

    /** Base firmware (vs0/os0) is complete. */
    fun isFirmwareInstalled(vitaRoot: File): Boolean {
        if (!keyFilePresent(vitaRoot, SHELL_KEY_FILE)) return false
        if (!hasMarkerFile(vitaRoot)) return true // legacy install, unverifiable
        val marker = readMarker(vitaRoot) ?: return false
        return (marker.fileCounts["vs0"] ?: 0L) > 0L
    }

    /** The fonts/update package (sa0) is installed. */
    fun isFontPackageInstalled(vitaRoot: File): Boolean {
        if (!keyFilePresent(vitaRoot, FONT_KEY_FILE)) return false
        if (!hasMarkerFile(vitaRoot)) return true // legacy install, unverifiable
        val marker = readMarker(vitaRoot) ?: return false
        return (marker.fileCounts["sa0"] ?: 0L) > 0L
    }

    /** Installed version string from the marker, if any. */
    fun installedVersion(vitaRoot: File): String? = readMarker(vitaRoot)?.version?.takeIf { it.isNotBlank() }
}

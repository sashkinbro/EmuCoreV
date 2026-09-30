package com.sbro.emucorev.core

import android.content.Context
import android.util.Log
import java.io.File
import java.security.MessageDigest
import java.util.zip.ZipFile

/**
 * Identifies the native emulator core shipped with this installation.
 *
 * The fingerprint is derived from the packaged core library itself, so it changes
 * automatically whenever the emulator core is rebuilt and never needs manual
 * bookkeeping per release.
 */
object CoreBinaryFingerprint {

    private const val TAG = "CoreFingerprint"
    private const val HASH_BUFFER_BYTES = 64 * 1024
    private const val CORE_LIBRARY_NAME = "Vita3K"

    fun current(context: Context): String? = runCatching {
        val libraryName = System.mapLibraryName(CORE_LIBRARY_NAME)
        packagedFingerprint(context, libraryName) ?: extractedFingerprint(context, libraryName)
    }.onFailure { error ->
        Log.w(TAG, "Unable to compute the core fingerprint", error)
    }.getOrNull()

    private fun packagedFingerprint(context: Context, libraryName: String): String? {
        val apkPaths = buildList {
            context.applicationInfo.sourceDir?.let(::add)
            context.applicationInfo.splitSourceDirs?.let(::addAll)
        }.filter { path -> path.isNotBlank() && File(path).isFile }

        apkPaths.forEach { path ->
            val entry = runCatching {
                ZipFile(path).use { zip ->
                    zip.entries().asSequence().firstOrNull { it.name.endsWith("/$libraryName") }
                }
            }.getOrNull() ?: return@forEach
            return "zip:${entry.crc.toString(16)}:${entry.size}"
        }
        return null
    }

    private fun extractedFingerprint(context: Context, libraryName: String): String? {
        val library = File(context.applicationInfo.nativeLibraryDir, libraryName)
        if (!library.isFile) return null
        val digest = MessageDigest.getInstance("SHA-256")
        library.inputStream().use { input ->
            val buffer = ByteArray(HASH_BUFFER_BYTES)
            while (true) {
                val read = input.read(buffer)
                if (read <= 0) break
                digest.update(buffer, 0, read)
            }
        }
        return "file:${digest.digest().joinToString("") { "%02x".format(it) }}:${library.length()}"
    }
}

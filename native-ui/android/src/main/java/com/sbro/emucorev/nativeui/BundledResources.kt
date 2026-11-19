package com.sbro.emucorev.nativeui

import android.content.Context
import java.io.File
import java.io.RandomAccessFile
import java.security.MessageDigest
import java.util.zip.ZipInputStream

/** Installs only the bundled UI pack; firmware and the emulator are never consulted. */
object BundledResources {
    @Synchronized
    fun prepare(context: Context): File {
        val assets = context.assets
        val version = assets.open("native-ui.sha256").bufferedReader().use { it.readText().trim() }
        require(version.matches(Regex("[0-9a-f]{64}"))) { "Invalid UI pack version" }
        val base = File(context.noBackupFilesDir, "native-ui-resources")
        check(base.isDirectory || base.mkdirs()) { "Cannot create UI resource directory" }
        RandomAccessFile(File(base, "install.lock"), "rw").channel.use { channel ->
            channel.lock().use {
                val destination = File(base, version)
                if (File(destination, ".complete").readTextOrNull() == version) return destination
                // A new pack cannot replace the previous ready directory until it is complete.
                val staging = File(base, "staging")
                check(!staging.exists() || staging.deleteRecursively()) { "Cannot remove partial UI pack" }
                check(staging.mkdirs()) { "Cannot stage UI pack" }
                try {
                    val digest = MessageDigest.getInstance("SHA-256")
                    assets.open("native-ui.zip").use { input ->
                        val buffer = ByteArray(65536)
                        while (true) {
                            val count = input.read(buffer)
                            if (count < 0) break
                            digest.update(buffer, 0, count)
                        }
                    }
                    check(digest.digest().joinToString("") { "%02x".format(it) } == version) {
                        "Corrupt UI resource archive"
                    }
                    var total = 0L
                    var entries = 0
                    val prefix = staging.canonicalPath + File.separator
                    ZipInputStream(assets.open("native-ui.zip")).use { zip ->
                        val buffer = ByteArray(65536)
                        while (true) {
                            val entry = zip.nextEntry ?: break
                            check(++entries <= 20000) { "Too many UI resources" }
                            val output = File(staging, entry.name)
                            check(output.canonicalPath.startsWith(prefix) && entry.name != ".complete") {
                                "Invalid UI resource path"
                            }
                            if (entry.isDirectory) {
                                check(output.isDirectory || output.mkdirs())
                            } else {
                                check(output.parentFile!!.isDirectory || output.parentFile!!.mkdirs())
                                check(!output.exists()) { "Duplicate UI resource" }
                                output.outputStream().use { file ->
                                    while (true) {
                                        val count = zip.read(buffer)
                                        if (count < 0) break
                                        total += count
                                        check(total <= 512L * 1024 * 1024) { "UI resource pack too large" }
                                        file.write(buffer, 0, count)
                                    }
                                }
                            }
                            zip.closeEntry() // Also checks the ZIP entry CRC.
                        }
                    }
                    check(entries > 0) { "Empty UI resource archive" }
                    File(staging, ".complete").writeText(version)
                    check(!destination.exists() || destination.deleteRecursively())
                    check(staging.renameTo(destination)) { "Cannot activate UI resource pack" }
                    base.listFiles()?.filter { it.isDirectory && it != destination }?.forEach {
                        it.deleteRecursively()
                    }
                    return destination
                } catch (error: Exception) {
                    staging.deleteRecursively()
                    throw error
                }
            }
        }
    }

    private fun File.readTextOrNull(): String? = if (isFile) readText() else null
}

package com.sbro.emucorev.core

import java.io.File
import java.io.IOException
import java.nio.file.Files
import java.nio.file.StandardCopyOption

internal object AtomicTextFile {
    fun write(target: File, content: String) {
        write(target, content) { temporary, destination ->
            Files.move(
                temporary.toPath(), destination.toPath(),
                StandardCopyOption.ATOMIC_MOVE, StandardCopyOption.REPLACE_EXISTING
            )
        }
    }

    internal fun write(target: File, content: String, atomicMove: (File, File) -> Unit) {
        val parent = target.parentFile
            ?: throw IllegalArgumentException("Target must have a parent directory: $target")
        parent.mkdirs()
        val temporary = File.createTempFile(".${target.name}.", ".tmp", parent)
        try {
            temporary.outputStream().bufferedWriter(Charsets.UTF_8).use { writer ->
                writer.write(content)
            }
            try {
                atomicMove(temporary, target)
            } catch (error: IOException) {
                // Android's rename replaces a file without first unlinking it.
                // Files.move(REPLACE_EXISTING) may unlink before renaming on
                // external storage, leaving another process with ENOENT.
                // If replacement is unavailable, preserve the old file and fail.
                if (!temporary.renameTo(target)) throw error
            }
        } finally {
            temporary.delete()
        }
    }
}

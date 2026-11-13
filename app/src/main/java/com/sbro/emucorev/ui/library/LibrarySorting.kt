package com.sbro.emucorev.ui.library

import com.sbro.emucorev.data.InstalledVitaGame
import java.io.File
import java.io.IOException
import java.nio.file.FileVisitResult
import java.nio.file.Files
import java.nio.file.Path
import java.nio.file.SimpleFileVisitor
import java.nio.file.attribute.BasicFileAttributes
import java.text.Normalizer
import java.util.Locale

/** X library sort choices that apply to V's single Vita platform. */
enum class LibrarySortOption {
    TITLE_ASC, TITLE_DESC, RECENT_DESC, RECENT_ASC, SIZE_DESC, SIZE_ASC;

    val requiresSize: Boolean get() = this == SIZE_DESC || this == SIZE_ASC

    companion object {
        fun fromPreferenceValue(value: String?): LibrarySortOption =
            entries.firstOrNull { it.name == value } ?: TITLE_ASC
    }
}

internal data class LibraryGameSortMetadata(val updatedAt: Long = 0, val sizeBytes: Long = 0)

internal fun sortInstalledGames(
    games: List<InstalledVitaGame>,
    sortOption: LibrarySortOption,
    metadata: Map<String, LibraryGameSortMetadata> = emptyMap()
): List<InstalledVitaGame> {
    val titleOrder = compareBy<InstalledVitaGame> { sortToken(it.title) }
        .thenBy { sortToken(it.titleId) }.thenBy { it.installPath }
    val comparator = when (sortOption) {
        LibrarySortOption.TITLE_ASC -> titleOrder
        LibrarySortOption.TITLE_DESC -> compareByDescending<InstalledVitaGame> { sortToken(it.title) }
            .thenByDescending { sortToken(it.titleId) }.thenBy { it.installPath }
        LibrarySortOption.RECENT_DESC -> compareByDescending<InstalledVitaGame> { metadata[it.titleId]?.updatedAt ?: 0L }.then(titleOrder)
        LibrarySortOption.RECENT_ASC -> compareBy<InstalledVitaGame> { metadata[it.titleId]?.updatedAt ?: 0L }.then(titleOrder)
        LibrarySortOption.SIZE_DESC -> compareByDescending<InstalledVitaGame> { metadata[it.titleId]?.sizeBytes ?: 0L }.then(titleOrder)
        LibrarySortOption.SIZE_ASC -> compareBy<InstalledVitaGame> { metadata[it.titleId]?.sizeBytes ?: 0L }.then(titleOrder)
    }
    return games.sortedWith(comparator)
}

/** Read metadata only on IO; never follow symlinks out of an installed game. */
internal fun readInstalledGameSortMetadata(
    game: InstalledVitaGame,
    includeSize: Boolean,
    checkCancelled: () -> Unit = {}
): LibraryGameSortMetadata {
    val directory = File(game.installPath)
    val roots = buildList {
        add(directory)
        // Match V's active metadata/deletion layout, including nonstandard base folder names.
        val ux0 = directory.parentFile?.takeIf { it.name.equals("app", ignoreCase = true) }?.parentFile
        val id = game.titleId.takeIf { it.isNotBlank() && it != "." && it != ".." && '/' !in it && '\\' !in it }
        if (ux0 != null && id != null) {
            add(File(ux0, "patch/$id"))
            add(File(ux0, "addcont/$id"))
        }
    }
    val updatedAt = roots.maxOf { try { it.lastModified() } catch (_: SecurityException) { 0L } }
    if (!includeSize) return LibraryGameSortMetadata(updatedAt)
    var bytes = 0L
    val visitor = object : SimpleFileVisitor<Path>() {
        override fun preVisitDirectory(dir: Path, attrs: BasicFileAttributes): FileVisitResult {
            checkCancelled()
            return FileVisitResult.CONTINUE
        }

        override fun visitFile(file: Path, attrs: BasicFileAttributes): FileVisitResult {
            checkCancelled()
            if (attrs.isRegularFile) {
                val size = attrs.size().coerceAtLeast(0)
                bytes = if (Long.MAX_VALUE - bytes < size) Long.MAX_VALUE else bytes + size
            }
            return FileVisitResult.CONTINUE
        }

        override fun visitFileFailed(file: Path, exc: IOException): FileVisitResult {
            checkCancelled()
            return FileVisitResult.CONTINUE
        }
    }
    roots.forEach { root ->
        checkCancelled()
        try {
            Files.walkFileTree(root.toPath(), visitor)
        } catch (_: IOException) {
            // A deleted/unreadable install contributes only metadata that could be read.
        } catch (_: SecurityException) {
            // Keep other installed content usable if a mount rejects this root.
        }
    }
    return LibraryGameSortMetadata(updatedAt, bytes)
}

private val combiningMarks = Regex("\\p{InCombiningDiacriticalMarks}+")
private fun sortToken(value: String): String = Normalizer.normalize(value, Normalizer.Form.NFD)
    .replace(combiningMarks, "").lowercase(Locale.ROOT).trim()

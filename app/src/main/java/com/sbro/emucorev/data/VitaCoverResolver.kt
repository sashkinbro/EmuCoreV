package com.sbro.emucorev.data

import java.io.File
import java.net.URI

data class ResolvedVitaCover(
    val path: String?,
    val is3d: Boolean = false,
    val igdbId: Long? = null,
    val publicUrl: String? = null
)

/** Candidate artwork and library icons have separate roles; custom art wins. */
internal class VitaCoverResolver(
    private val index: VitaCoverIndex,
    private val cache: CoverDiskCache,
    private val usableLocal: (String) -> Boolean
) {
    suspend fun resolve(titleId: String?, igdbId: Long?, candidatePath: String?, fallbackIconPath: String?,
                        style: VitaCoverStyle, cachedOnly: Boolean = false): ResolvedVitaCover {
        val custom = candidatePath?.takeIf { it.isNotBlank() && isLocal(it) &&
            !cache.isManagedPath(localFile(it)?.path ?: it) && !sameLocal(it, fallbackIconPath) && usableLocal(it) }
        if (custom != null) return ResolvedVitaCover(custom)
        val asset = index.find(titleId, igdbId, candidatePath, style)
        if (asset != null) {
            (if (cachedOnly) cache.findCached(asset) else cache.resolve(asset))?.let { file ->
                return ResolvedVitaCover(file.absolutePath, style == VitaCoverStyle.THREE_D, asset.igdbId, asset.url)
            }
        }
        return ResolvedVitaCover(null)
    }

    companion object {
        fun localFile(path: String): File? = runCatching {
            when {
                path.startsWith("file:") -> File(URI(path))
                path.contains("://") -> null
                else -> File(path)
            }
        }.getOrNull()

        private fun isLocal(path: String): Boolean = path.startsWith("content://") || localFile(path) != null

        private fun sameLocal(first: String, second: String?): Boolean {
            if (second == null) return false
            if (first == second) return true
            val firstFile = localFile(first) ?: return false
            val secondFile = localFile(second) ?: return false
            return runCatching { firstFile.canonicalFile == secondFile.canonicalFile }.getOrDefault(false)
        }
    }
}

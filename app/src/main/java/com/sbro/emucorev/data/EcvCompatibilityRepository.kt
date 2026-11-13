package com.sbro.emucorev.data

import android.content.Context
import java.io.File
import java.nio.file.Files
import java.nio.file.LinkOption

/** Android asset/cache adapter for the owned compatibility dataset. */
class EcvCompatibilityRepository(context: Context) {
    private val appContext = context.applicationContext
    private val delegate = EcvCompatibilityDatasetRepository(
        cacheFile = File(appContext.filesDir, "$CACHE_DIRECTORY/$CACHE_FILE_NAME"),
        bundledSource = { appContext.assets.open(BUNDLED_ASSET_PATH) }
    )

    fun load(): EcvCompatibilityDataset {
        cleanupObsoleteCompatibilityFiles(appContext.filesDir)
        return delegate.load()
    }

    companion object {
        const val BUNDLED_ASSET_PATH = "catalog/ecv_compatibility.json"
        const val CACHE_DIRECTORY = "ecv_compatibility"
        const val CACHE_FILE_NAME = "cache-v1.json"
    }
}

/** Removes only six retired cache files. No directory traversal, recursion or symlink following. */
internal fun cleanupObsoleteCompatibilityFiles(filesDir: File) {
    val ownedRoot = File(filesDir, "compatibility")
    runCatching {
        val root = ownedRoot.absoluteFile.toPath()
        var ancestor: java.nio.file.Path? = root
        while (ancestor != null) {
            if (Files.isSymbolicLink(ancestor)) return
            ancestor = ancestor.parent
        }
        if (!Files.isDirectory(root, LinkOption.NOFOLLOW_LINKS) || Files.isSymbolicLink(root)) return
        if (ownedRoot.canonicalFile.parentFile != filesDir.canonicalFile) return
        OBSOLETE_COMPATIBILITY_FILES.forEach { name ->
            val target = File(ownedRoot, name)
            val path = target.toPath()
            if (Files.isRegularFile(path, LinkOption.NOFOLLOW_LINKS) && !Files.isSymbolicLink(path) &&
                target.canonicalFile.parentFile == ownedRoot.canonicalFile) {
                runCatching { Files.deleteIfExists(path) }
            }
        }
    }
}

private val OBSOLETE_COMPATIBILITY_FILES = listOf(
    "app_compat_db.xml", "commercial_list.json", "app_compat_meta.properties",
    "app_compat_db.tmp.zip", "app_compat_db.tmp.xml", "commercial_list.tmp.json"
)

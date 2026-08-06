package com.sbro.emucorev.core

import android.content.Context
import android.net.Uri
import android.os.Environment
import android.provider.DocumentsContract
import androidx.core.net.toUri
import androidx.documentfile.provider.DocumentFile
import java.io.File
import android.util.Log

object DocumentPathResolver {

    fun getDisplayName(context: Context, rawPath: String): String {
        if (rawPath.startsWith("file://")) return File(rawPath.toUri().path.orEmpty()).name
        if (!rawPath.startsWith("content://")) return File(rawPath).name
        val uri = rawPath.toUri()
        return DocumentFile.fromSingleUri(context, uri)?.name
            ?: uri.lastPathSegment?.substringAfterLast(':')
            ?: rawPath.substringAfterLast('/')
    }

    fun resolveFilePath(context: Context, rawPath: String, copyToCache: Boolean = false): String? {
        if (rawPath.startsWith("file://")) return rawPath.toUri().path
        if (!rawPath.startsWith("content://")) return rawPath

        val uri = rawPath.toUri()
        val directPath = resolveExternalStoragePath(uri)
        if (directPath != null && File(directPath).canRead()) {
            return directPath
        }

        val fileName = getDisplayName(context, rawPath)

        // Prefer resolving to a real on-disk path before falling back to a full
        // byte-for-byte copy. Install payloads are game-sized, so an avoidable
        // copy costs the user gigabytes of duplicated storage.
        val treePath = findFileInPersistedTree(context, uri, fileName)
        if (treePath != null) return treePath

        if (copyToCache) {
            return copyUriToCache(context, uri, fileName)
        }

        return null
    }

    /**
     * Deletes a staged install payload previously produced by [resolveFilePath].
     *
     * No-op when [path] is not inside the staging directory, so it is safe to
     * call with a directly-resolved source path that must not be removed.
     */
    fun cleanupStagedFile(context: Context, path: String?) {
        if (path.isNullOrBlank()) return
        runCatching {
            val staged = File(path)
            val stagingDir = EmulatorStorage.installStagingRoot(context)
            if (staged.isFile && staged.canonicalPath.startsWith(stagingDir.canonicalPath)) {
                staged.delete()
            }
        }.onFailure { error ->
            Log.w("DocumentPathResolver", "Failed to clean staged install file: $path", error)
        }
    }

    private fun copyUriToCache(context: Context, uri: Uri, fileName: String): String? {
        return try {
            val cacheDir = EmulatorStorage.installStagingRoot(context)
            if (!cacheDir.exists()) cacheDir.mkdirs()
            val destFile = File(cacheDir, fileName)
            if (destFile.exists()) destFile.delete()
            
            val inputStream = context.contentResolver.openInputStream(uri) ?: return null
            inputStream.use { input ->
                destFile.outputStream().use { output ->
                    input.copyTo(output)
                }
            }
            destFile.takeIf { it.isFile }?.absolutePath
        } catch (e: Exception) {
            Log.e("DocumentPathResolver", "Failed to copy URI to cache: $uri", e)
            null
        }
    }

    private fun resolveExternalStoragePath(uri: Uri): String? {
        val documentId = runCatching { DocumentsContract.getDocumentId(uri) }.getOrNull()
            ?: runCatching { DocumentsContract.getTreeDocumentId(uri) }.getOrNull()
            ?: return null

        val parts = documentId.split(':', limit = 2)
        if (parts.isEmpty()) return null

        val volume = parts[0]
        val relativePath = parts.getOrNull(1).orEmpty()

        return when {
            volume.equals("primary", ignoreCase = true) -> {
                val base = Environment.getExternalStorageDirectory()
                if (relativePath.isBlank()) base.absolutePath else File(base, relativePath).absolutePath
            }

            volume.equals("home", ignoreCase = true) -> {
                val base = File(Environment.getExternalStorageDirectory(), "Documents")
                if (relativePath.isBlank()) base.absolutePath else File(base, relativePath).absolutePath
            }

            volume.startsWith("/") -> volume
            else -> null
        }
    }

    private fun findFileInPersistedTree(context: Context, targetUri: Uri, fileName: String): String? {
        val persistedTrees = context.contentResolver.persistedUriPermissions
            .mapNotNull { permission -> DocumentFile.fromTreeUri(context, permission.uri) }

        for (tree in persistedTrees) {
            val resolved = findFileRecursive(tree, targetUri, fileName)
            if (resolved != null) return resolved
        }

        return null
    }

    private fun findFileRecursive(root: DocumentFile, targetUri: Uri, fileName: String): String? {
        for (child in root.listFiles()) {
            if (child.uri == targetUri) {
                return resolveExternalStoragePath(child.uri)
            }

            if (child.isDirectory) {
                val nested = findFileRecursive(child, targetUri, fileName)
                if (nested != null) return nested
            } else if (child.name == fileName) {
                val direct = resolveExternalStoragePath(child.uri)
                if (direct != null) return direct
            }
        }

        return null
    }
}

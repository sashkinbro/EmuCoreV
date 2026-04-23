package com.sbro.emucorev.core.vita.provider

import android.database.Cursor
import android.database.MatrixCursor
import android.os.CancellationSignal
import android.os.ParcelFileDescriptor
import android.provider.DocumentsContract
import android.provider.DocumentsProvider
import android.webkit.MimeTypeMap
import com.sbro.emucorev.R
import java.io.File
import java.io.FileNotFoundException

class VitaDocumentsProvider : DocumentsProvider() {
    private val root = "VitaRoot"

    private val defaultRootProjection = arrayOf(
        DocumentsContract.Root.COLUMN_ROOT_ID,
        DocumentsContract.Root.COLUMN_MIME_TYPES,
        DocumentsContract.Root.COLUMN_FLAGS,
        DocumentsContract.Root.COLUMN_TITLE,
        DocumentsContract.Root.COLUMN_DOCUMENT_ID,
        DocumentsContract.Root.COLUMN_AVAILABLE_BYTES,
        DocumentsContract.Root.COLUMN_ICON
    )

    private val defaultDocumentProjection = arrayOf(
        DocumentsContract.Document.COLUMN_DOCUMENT_ID,
        DocumentsContract.Document.COLUMN_MIME_TYPE,
        DocumentsContract.Document.COLUMN_DISPLAY_NAME,
        DocumentsContract.Document.COLUMN_LAST_MODIFIED,
        DocumentsContract.Document.COLUMN_FLAGS,
        DocumentsContract.Document.COLUMN_SIZE,
        DocumentsContract.Document.COLUMN_ICON
    )

    override fun onCreate(): Boolean = true

    private fun getStorageDir(): File {
        return File(context?.getExternalFilesDir(null), "vita").apply {
            if (!exists()) mkdirs()
        }
    }

    @Throws(FileNotFoundException::class)
    private fun resolveFile(documentId: String): File {
        if (!documentId.startsWith(root)) {
            throw FileNotFoundException("$documentId was not found")
        }
        val file = File(getStorageDir(), documentId.substring(root.length + 1))
        if (!file.exists()) {
            throw FileNotFoundException("$documentId was not found")
        }
        return file
    }

    private fun getDocumentId(file: File): String {
        return root + ":" + file.absolutePath.substring(getStorageDir().absolutePath.length)
    }

    @Throws(FileNotFoundException::class)
    private fun applyCursor(cursor: MatrixCursor, file: File?, documentId: String?) {
        val resolvedFile = file ?: resolveFile(documentId.orEmpty())
        val resolvedDocumentId = documentId ?: getDocumentId(resolvedFile)
        val isRoot = resolvedFile == getStorageDir()
        val flags = if (resolvedFile.isDirectory) {
            DocumentsContract.Document.FLAG_DIR_SUPPORTS_CREATE or
                DocumentsContract.Document.FLAG_SUPPORTS_DELETE or
                DocumentsContract.Document.FLAG_SUPPORTS_REMOVE or
                DocumentsContract.Document.FLAG_SUPPORTS_RENAME
        } else {
            DocumentsContract.Document.FLAG_SUPPORTS_COPY or
                DocumentsContract.Document.FLAG_SUPPORTS_DELETE or
                DocumentsContract.Document.FLAG_SUPPORTS_MOVE or
                DocumentsContract.Document.FLAG_SUPPORTS_REMOVE or
                DocumentsContract.Document.FLAG_SUPPORTS_RENAME or
                DocumentsContract.Document.FLAG_SUPPORTS_WRITE
        }

        val row = cursor.newRow()
        row.add(DocumentsContract.Document.COLUMN_DOCUMENT_ID, resolvedDocumentId)
            .add(DocumentsContract.Document.COLUMN_DISPLAY_NAME, if (isRoot) "Vita3K" else resolvedFile.name)
            .add(DocumentsContract.Document.COLUMN_SIZE, resolvedFile.length())
            .add(DocumentsContract.Document.COLUMN_MIME_TYPE, getMimeType(resolvedFile))
            .add(DocumentsContract.Document.COLUMN_LAST_MODIFIED, resolvedFile.lastModified())
            .add(DocumentsContract.Document.COLUMN_FLAGS, flags)
        if (isRoot) {
            row.add(DocumentsContract.Document.COLUMN_ICON, R.mipmap.ic_launcher)
        }
    }

    override fun queryRoots(projection: Array<out String>?): Cursor {
        val cursor = MatrixCursor(projection ?: defaultRootProjection)
        val storageDir = getStorageDir()
        cursor.newRow()
            .add(DocumentsContract.Root.COLUMN_ROOT_ID, root)
            .add(
                DocumentsContract.Root.COLUMN_FLAGS,
                DocumentsContract.Root.FLAG_SUPPORTS_CREATE or DocumentsContract.Root.FLAG_SUPPORTS_IS_CHILD
            )
            .add(DocumentsContract.Root.COLUMN_TITLE, "Vita3K")
            .add(DocumentsContract.Root.COLUMN_DOCUMENT_ID, getDocumentId(storageDir))
            .add(DocumentsContract.Root.COLUMN_MIME_TYPES, "*/*")
            .add(DocumentsContract.Root.COLUMN_AVAILABLE_BYTES, storageDir.freeSpace)
            .add(DocumentsContract.Root.COLUMN_ICON, R.mipmap.ic_launcher)
        return cursor
    }

    @Throws(FileNotFoundException::class)
    override fun queryDocument(documentId: String, projection: Array<out String>?): Cursor {
        val cursor = MatrixCursor(projection ?: defaultDocumentProjection)
        applyCursor(cursor, null, documentId)
        return cursor
    }

    @Throws(FileNotFoundException::class)
    override fun queryChildDocuments(
        parentDocumentId: String,
        projection: Array<out String>?,
        sortOrder: String?
    ): Cursor {
        val cursor = MatrixCursor(projection ?: defaultDocumentProjection)
        resolveFile(parentDocumentId).listFiles().orEmpty().forEach { child ->
            applyCursor(cursor, child, null)
        }
        return cursor
    }

    @Throws(FileNotFoundException::class)
    override fun openDocument(
        documentId: String,
        mode: String,
        signal: CancellationSignal?
    ): ParcelFileDescriptor {
        return ParcelFileDescriptor.open(resolveFile(documentId), ParcelFileDescriptor.parseMode(mode))
    }

    @Throws(FileNotFoundException::class)
    override fun createDocument(parentDocumentId: String, mimeType: String, displayName: String): String {
        val parent = resolveFile(parentDocumentId)
        var conflicts = 1
        var newFile = File(parent, displayName)
        while (newFile.exists()) {
            newFile = File(parent, "$displayName (${++conflicts})")
        }

        val success = try {
            if (mimeType == DocumentsContract.Document.MIME_TYPE_DIR) newFile.mkdir() else newFile.createNewFile()
        } catch (_: Exception) {
            false
        }

        if (!success) {
            throw FileNotFoundException()
        }
        return getDocumentId(newFile)
    }

    @Throws(FileNotFoundException::class)
    override fun deleteDocument(documentId: String) {
        if (!resolveFile(documentId).delete()) {
            throw FileNotFoundException()
        }
    }

    @Throws(FileNotFoundException::class)
    override fun getDocumentType(documentId: String): String {
        return getMimeType(resolveFile(documentId))
    }

    override fun isChildDocument(parentDocumentId: String, documentId: String): Boolean {
        return documentId.startsWith(parentDocumentId)
    }

    private companion object {
        fun getMimeType(file: File): String {
            if (file.isDirectory) {
                return DocumentsContract.Document.MIME_TYPE_DIR
            }
            val extension = file.name.substringAfterLast('.', "").lowercase()
            return MimeTypeMap.getSingleton().getMimeTypeFromExtension(extension)
                ?: "application/octet-stream"
        }
    }
}

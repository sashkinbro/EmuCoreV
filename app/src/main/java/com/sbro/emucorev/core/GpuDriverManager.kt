package com.sbro.emucorev.core

import android.content.Context
import android.net.Uri
import java.io.File
import java.io.FileOutputStream
import java.util.zip.ZipInputStream

data class InstalledGpuDriver(
    val name: String,
    val mainLibrary: String,
    val isUsable: Boolean
)

class GpuDriverManager(private val context: Context) {

    fun listInstalledDrivers(): List<InstalledGpuDriver> {
        val root = driversRoot()
        if (!root.exists()) return emptyList()
        return root.listFiles()
            .orEmpty()
            .filter { it.isDirectory }
            .mapNotNull { driverDir ->
                val mainLibrary = readMainLibraryName(driverDir) ?: return@mapNotNull null
                InstalledGpuDriver(
                    name = driverDir.name,
                    mainLibrary = mainLibrary,
                    isUsable = File(driverDir, mainLibrary).isFile
                )
            }
            .sortedBy { it.name.lowercase() }
    }

    fun installFromArchive(uri: Uri): String {
        val archiveName = queryDisplayName(uri)
            ?: uri.lastPathSegment
            ?: "custom-driver.zip"
        val driverName = archiveName.substringBeforeLast('.').ifBlank { "custom-driver" }
        val targetDir = File(driversRoot(), driverName)

        if (targetDir.exists()) {
            targetDir.deleteRecursively()
        }
        targetDir.mkdirs()

        val extractedFiles = mutableListOf<String>()
        context.contentResolver.openInputStream(uri)?.use { input ->
            ZipInputStream(input).use { zip ->
                generateSequence { zip.nextEntry }.forEach { entry ->
                    val entryName = entry.name ?: return@forEach
                    if (entry.isDirectory) return@forEach
                    val normalizedEntryName = entryName.replace('\\', '/').trimStart('/')
                    if (normalizedEntryName.isBlank() || normalizedEntryName.contains("..")) {
                        error("Archive contains an invalid file path")
                    }
                    val outFile = File(targetDir, normalizedEntryName)
                    val canonicalTarget = targetDir.canonicalFile
                    val canonicalOutFile = outFile.canonicalFile
                    if (!canonicalOutFile.toPath().startsWith(canonicalTarget.toPath())) {
                        error("Archive contains an invalid file path")
                    }
                    outFile.parentFile?.mkdirs()
                    FileOutputStream(outFile).use { output ->
                        zip.copyTo(output)
                    }
                    extractedFiles += normalizedEntryName
                }
            }
        } ?: error("Could not open archive")

        val mainDriverFile = selectMainDriverFile(extractedFiles)
        val selectedDriver = mainDriverFile ?: run {
            targetDir.deleteRecursively()
            error("Archive does not contain a Vulkan driver file")
        }

        File(targetDir, "driver_name.txt").writeText("$selectedDriver\n")
        return driverName
    }

    fun remove(driverName: String) {
        File(driversRoot(), driverName).deleteRecursively()
    }

    private fun driversRoot(): File = File(context.filesDir, "driver")

    fun readMainLibraryName(driverName: String): String? = readMainLibraryName(File(driversRoot(), driverName))

    private fun readMainLibraryName(driverDir: File): String? {
        val metadataFile = File(driverDir, "driver_name.txt")
        if (!metadataFile.isFile) return null
        return metadataFile.readText()
            .lineSequence()
            .map(String::trim)
            .firstOrNull { it.isNotEmpty() }
    }

    private fun selectMainDriverFile(extractedFiles: List<String>): String? {
        val sharedLibraries = extractedFiles
            .filter { it.endsWith(".so", ignoreCase = true) }
        if (sharedLibraries.isEmpty()) return null

        return sharedLibraries.firstOrNull { file ->
            val name = file.substringAfterLast('/')
            name.equals("libvulkan.so", ignoreCase = true)
        } ?: sharedLibraries.firstOrNull { file ->
            val name = file.substringAfterLast('/')
            name.startsWith("vulkan.", ignoreCase = true) || name.startsWith("libvulkan.", ignoreCase = true)
        } ?: sharedLibraries.firstOrNull { file ->
            file.contains("vulkan", ignoreCase = true)
        }
    }

    private fun queryDisplayName(uri: Uri): String? {
        val projection = arrayOf(android.provider.OpenableColumns.DISPLAY_NAME)
        return context.contentResolver.query(uri, projection, null, null, null)?.use { cursor ->
            val index = cursor.getColumnIndex(android.provider.OpenableColumns.DISPLAY_NAME)
            if (index >= 0 && cursor.moveToFirst()) cursor.getString(index) else null
        }
    }
}

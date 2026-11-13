package com.sbro.emucorev.core

import android.content.Context
import android.net.Uri
import android.provider.OpenableColumns
import org.json.JSONArray
import org.json.JSONObject
import java.io.File
import java.io.InputStream
import java.io.RandomAccessFile
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.nio.file.Files
import java.security.MessageDigest
import java.util.UUID
import java.util.zip.ZipInputStream

data class InstalledGpuDriver(
    val name: String, val mainLibrary: String, val mainLibraryPath: String, val isUsable: Boolean,
    val vendor: GpuDriverVendor = GpuDriverVendor.UNKNOWN,
    val family: GpuDriverFamily = GpuDriverFamily.OTHER,
    val supportedGpuModels: List<String> = emptyList(),
    val fileState: GpuDriverFileState = GpuDriverFileState.UNKNOWN,
    val nativeCapability: GpuDriverNativeCapability = GpuDriverNativeCapability.UNKNOWN,
    val catalogId: String? = null, val version: String = "", val tag: String = "", val gpu: String = ""
)

class GpuDriverManager(private val context: Context) {
    private val store = GpuDriverStore(File(context.filesDir, "driver")) { DeviceGpuInfoProvider.get(context) }
    fun listInstalledDrivers(): List<InstalledGpuDriver> = store.listInstalledDrivers()
    fun installFromArchive(uri: Uri, expectedVendor: GpuDriverVendor? = null, catalogDriver: RemoteGpuDriver? = null): String {
        val name = queryDisplayName(uri) ?: uri.lastPathSegment ?: "custom-driver.zip"
        val input = context.contentResolver.openInputStream(uri) ?: error("Could not open archive")
        return input.use { store.installFromArchive(it, name, expectedVendor, catalogDriver) }
    }
    fun installFromArchive(file: File, expectedVendor: GpuDriverVendor? = null, catalogDriver: RemoteGpuDriver? = null): String =
        file.inputStream().use { store.installFromArchive(it, file.name, expectedVendor, catalogDriver) }
    fun remove(driverName: String) = store.remove(driverName)
    fun readMainLibraryPath(driverName: String): String? = store.readMainLibraryPath(driverName)
    /** null/blank means System. Never substitute a different installed package. */
    fun resolveUsableDriverPath(preferredPath: String?): String? = store.resolveUsableDriverPath(preferredPath)
    fun isVendorCompatible(libraryFile: File): Boolean = store.resolveUsableDriverPath(libraryFile.absolutePath) != null
    private fun queryDisplayName(uri: Uri): String? = context.contentResolver
        .query(uri, arrayOf(OpenableColumns.DISPLAY_NAME), null, null, null)?.use { cursor ->
            val index = cursor.getColumnIndex(OpenableColumns.DISPLAY_NAME)
            if (index >= 0 && cursor.moveToFirst()) cursor.getString(index) else null
        }
}

/** Real filesystem operations separated from Android URI acquisition for deterministic tests. */
internal class GpuDriverStore(private val root: File, private val device: () -> DeviceGpuSnapshot) {
    fun listInstalledDrivers(): List<InstalledGpuDriver> = synchronized(mutations) {
        root.listFiles().orEmpty().filter { it.isDirectory && !it.name.startsWith(".install-") &&
            !it.name.startsWith(".previous-") && !Files.isSymbolicLink(it.toPath()) }
            .mapNotNull { readInstalled(it.name) }.sortedBy { it.name.lowercase() }
    }
    fun remove(name: String) = synchronized(mutations) {
        driverDirectory(name)?.takeIf { it.exists() }?.deleteRecursively()
        Unit
    }
    fun readMainLibraryPath(name: String): String? = synchronized(mutations) {
        readInstalled(name)?.takeIf { it.isUsable }?.mainLibraryPath
    }
    fun resolveUsableDriverPath(preferredPath: String?): String? = synchronized(mutations) {
        if (preferredPath.isNullOrBlank()) return@synchronized null
        val requested = File(preferredPath).absoluteFile.toPath()
        if (requested.any { it.toString() == "." || it.toString() == ".." }) return@synchronized null
        val base = root.absoluteFile.toPath().takeIf { requested.startsWith(it) }
            ?: root.canonicalFile.toPath().takeIf { requested.startsWith(it) } ?: return@synchronized null
        val relative = base.relativize(requested)
        if (relative.nameCount < 2) return@synchronized null
        val installed = readInstalled(relative.getName(0).toString()) ?: return@synchronized null
        val declared = File(installed.name, installed.mainLibrary).toPath()
        if (relative != declared || File(preferredPath).canonicalPath != installed.mainLibraryPath) return@synchronized null
        installed.takeIf { it.isUsable }?.mainLibraryPath
    }
    fun installFromArchive(input: InputStream, archiveName: String, expectedVendor: GpuDriverVendor? = null,
        catalogDriver: RemoteGpuDriver? = null): String = synchronized(mutations) {
        val name = archiveName.substringBeforeLast('.').ifBlank { "custom-driver" }
        val target = driverDirectory(name) ?: error("Archive contains an invalid driver name")
        check(root.isDirectory || root.mkdirs()) { "Could not create driver directory" }
        val staging = File(root, ".install-${UUID.randomUUID()}").apply { check(mkdir()) }
        val archive = File(root, ".archive-${UUID.randomUUID()}.zip")
        var backup: File? = null
        try {
            archive.outputStream().use { output -> input.copyTo(output) }
            if (catalogDriver?.archiveSha256 != null && gpuDriverSha256(archive) != catalogDriver.archiveSha256.lowercase()) {
                error("Driver archive checksum does not match the catalog package")
            }
            val extracted = mutableListOf<String>()
            ZipInputStream(archive.inputStream()).use { zip ->
                generateSequence { zip.nextEntry }.forEach { entry ->
                    if (entry.isDirectory) return@forEach
                    val relative = entry.name.replace('\\', '/')
                    val output = managedLibrary(staging, relative) ?: error("Archive contains an invalid file path")
                    if (relative.substringAfterLast('/') in setOf(MAIN_NAME, PACKAGE_METADATA, "driver_vendor.txt")) return@forEach
                    output.parentFile?.mkdirs()
                    output.outputStream().use { zip.copyTo(it) }
                    extracted += relative
                }
            }
            val main = selectMainDriverFile(extracted) ?: error("Archive does not contain a Vulkan driver file")
            val library = managedLibrary(staging, main) ?: error("Archive contains an invalid driver file")
            if (!isValidDriverLibrary(library)) error("Archive contains a corrupt or non-arm64 Vulkan driver file")
            val detected = GpuDriverCompatibility.vendorOfDriverLibrary(library)
            val declared = catalogDriver?.vendor?.takeIf { it != GpuDriverVendor.UNKNOWN }
                ?: expectedVendor?.takeIf { it != GpuDriverVendor.UNKNOWN }
            if (detected != GpuDriverVendor.UNKNOWN && declared != null && detected != declared) {
                error("Driver library vendor does not match the package declaration")
            }
            val vendor = declared ?: detected
            val family = catalogDriver?.family ?: GpuDriverCompatibility.familyOfDriverLibrary(library)
            val exact = catalogDriver?.takeIf { it.id.isNotBlank() && it.version.isNotBlank() && it.tag.isNotBlank() }
            val installed = InstalledGpuDriver(name, main, library.canonicalPath, false, vendor, family,
                exact?.supportedGpuModels.orEmpty(), GpuDriverFileState.VALID, catalogId = exact?.id,
                version = exact?.version.orEmpty(), tag = exact?.tag.orEmpty(), gpu = exact?.gpu.orEmpty())
            if (GpuDriverCompatibility.installedCompatibility(installed, device()).state == GpuDriverCompatibilityState.INCOMPATIBLE) {
                error("This driver package is incompatible with the device GPU")
            }
            File(staging, MAIN_NAME).writeText("$main\n", Charsets.UTF_8)
            File(staging, PACKAGE_METADATA).writeText(JSONObject().apply {
                put("schema", 1); put("mainLibrary", main); put("librarySha256", gpuDriverSha256(library))
                put("vendor", vendor.name); put("family", family.name)
                put("catalogId", exact?.id.orEmpty()); put("version", exact?.version.orEmpty())
                put("tag", exact?.tag.orEmpty()); put("gpu", exact?.gpu.orEmpty())
                put("supportedGpuModels", JSONArray(exact?.supportedGpuModels.orEmpty()))
            }.toString(), Charsets.UTF_8)
            GpuDriverCompatibility.writeInstalledDriverVendor(library, vendor)
            permissions(library)
            if (target.exists()) {
                backup = File(root, ".previous-${UUID.randomUUID()}")
                check(target.renameTo(backup!!)) { "Could not replace installed driver" }
            }
            if (!staging.renameTo(target)) {
                backup?.renameTo(target)
                error("Could not finish driver installation")
            }
            backup?.deleteRecursively()
            name
        } finally { staging.deleteRecursively(); archive.delete() }
    }

    private fun readInstalled(name: String): InstalledGpuDriver? {
        val dir = driverDirectory(name)?.takeIf { it.isDirectory } ?: return null
        val mainFile = File(dir, MAIN_NAME)
        val main = runCatching { mainFile.takeIf { it.isFile && !Files.isSymbolicLink(it.toPath()) && it.length() <= 4096 }
            ?.readText(Charsets.UTF_8)?.lineSequence()?.map(String::trim)?.firstOrNull(String::isNotEmpty) }.getOrNull() ?: return null
        val library = managedLibrary(dir, main) ?: return null
        var state = when {
            !library.isFile -> GpuDriverFileState.MISSING
            isValidDriverLibrary(library) -> GpuDriverFileState.VALID
            else -> GpuDriverFileState.INVALID
        }
        val metadataFile = File(dir, PACKAGE_METADATA)
        val metadata = runCatching { metadataFile.takeIf {
            it.isFile && !Files.isSymbolicLink(it.toPath()) && it.length() <= 65_536
        }?.readText(Charsets.UTF_8)?.let(::JSONObject) }.getOrNull()
        val bound = runCatching { metadata?.takeIf {
            it.optInt("schema") == 1 && it.optString("mainLibrary") == main && state == GpuDriverFileState.VALID &&
                it.optString("librarySha256") == gpuDriverSha256(library)
        } }.getOrNull()
        if (metadataFile.exists() && bound == null && state == GpuDriverFileState.VALID) state = GpuDriverFileState.UNKNOWN
        val vendor = bound?.optString("vendor")?.let { enumValueOrNull<GpuDriverVendor>(it) }
            ?: GpuDriverCompatibility.installedDriverVendor(library).takeIf { it != GpuDriverVendor.UNKNOWN }
            ?: GpuDriverCompatibility.vendorOfDriverLibrary(library)
        val family = bound?.optString("family")?.let { enumValueOrNull<GpuDriverFamily>(it) }
            ?: GpuDriverCompatibility.familyOfDriverLibrary(library)
        val exact = bound?.takeIf { it.optString("catalogId").isNotBlank() && it.optString("version").isNotBlank() &&
            it.optString("tag").isNotBlank() }
        val models = exact?.optJSONArray("supportedGpuModels")?.let { array ->
            (0 until array.length()).mapNotNull { (array.opt(it) as? String)?.takeIf(String::isNotBlank) }
        }.orEmpty()
        val result = InstalledGpuDriver(name, main, library.canonicalPath, false, vendor, family, models, state,
            catalogId = exact?.optString("catalogId"), version = exact?.optString("version").orEmpty(),
            tag = exact?.optString("tag").orEmpty(), gpu = exact?.optString("gpu").orEmpty())
        val policy = GpuDriverCompatibility.resolveSelection(name, null, device(), listOf(result))
        if (state == GpuDriverFileState.VALID) permissions(library)
        return result.copy(isUsable = policy.requestable)
    }
    private fun driverDirectory(name: String): File? {
        if (name.isBlank() || name in setOf(".", "..") || '/' in name || '\\' in name ||
            name.startsWith(".install-") || name.startsWith(".previous-")) return null
        return runCatching { File(root, name).takeIf {
            !Files.isSymbolicLink(it.toPath()) && it.canonicalFile.parentFile == root.canonicalFile
        } }.getOrNull()
    }
    private fun managedLibrary(dir: File, relative: String): File? {
        if (relative.isBlank() || relative.startsWith('/') || relative.startsWith('\\') ||
            relative.replace('\\', '/').split('/').any { it == ".." || it == "." } || ':' in relative) return null
        return runCatching { File(dir, relative).canonicalFile.takeIf {
            it.toPath().startsWith(dir.canonicalFile.toPath()) && it != dir.canonicalFile
        } }.getOrNull()
    }
    private fun selectMainDriverFile(files: List<String>): String? {
        val libraries = files.filter { it.endsWith(".so", true) }
        return libraries.firstOrNull { it.substringAfterLast('/').equals("libvulkan.so", true) }
            ?: libraries.firstOrNull { it.substringAfterLast('/').startsWith("vulkan.", true) ||
                it.substringAfterLast('/').startsWith("libvulkan.", true) }
            ?: libraries.firstOrNull { it.contains("vulkan", true) }
    }
    private fun permissions(file: File) {
        file.setReadable(true, true); file.setWritable(true, true); file.setExecutable(true, true)
    }
    companion object {
        private val mutations = Any()
        private const val MAIN_NAME = "driver_name.txt"
        private const val PACKAGE_METADATA = "driver_package.json"
    }
}

private inline fun <reified T : Enum<T>> enumValueOrNull(raw: String): T? = enumValues<T>().firstOrNull { it.name == raw }

internal fun gpuDriverSha256(file: File): String {
    val digest = MessageDigest.getInstance("SHA-256")
    file.inputStream().use { input ->
        val buffer = ByteArray(65_536)
        while (true) { val read = input.read(buffer); if (read < 0) break; if (read > 0) digest.update(buffer, 0, read) }
    }
    return digest.digest().joinToString("") { "%02x".format(it.toInt() and 0xff) }
}

/** ELF64/AArch64 shared-object structure, not proof of successful native loading. */
internal fun isValidDriverLibrary(file: File): Boolean {
    return try {
        if (!file.isFile || file.length() < 120) false else RandomAccessFile(file, "r").use { input ->
            val bytes = ByteArray(64).also(input::readFully)
            val header = ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN)
            if (bytes[0] != 0x7f.toByte() || bytes[1] != 'E'.code.toByte() || bytes[2] != 'L'.code.toByte() ||
                bytes[3] != 'F'.code.toByte() || bytes[4] != 2.toByte() || bytes[5] != 1.toByte() || bytes[6] != 1.toByte() ||
                header.getShort(16).toInt() != 3 || header.getShort(18).toInt() != 183 || header.getInt(20) != 1 ||
                header.getShort(52).toInt() != 64 || header.getShort(54).toInt() != 56) return false
            val offset = header.getLong(32)
            val count = header.getShort(56).toInt() and 0xffff
            if (offset < 64 || count == 0 || offset > input.length() - count * 56L) return false
            repeat(count) { index ->
                input.seek(offset + index * 56L)
                val segment = ByteBuffer.wrap(ByteArray(56).also(input::readFully)).order(ByteOrder.LITTLE_ENDIAN)
                val start = segment.getLong(8); val length = segment.getLong(32)
                if (start < 0 || length < 0 || start > input.length() || length > input.length() - start) return false
            }
            true
        }
    } catch (_: Exception) { false }
}

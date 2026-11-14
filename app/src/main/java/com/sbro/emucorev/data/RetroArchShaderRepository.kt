package com.sbro.emucorev.data

import android.content.Context
import android.net.Uri
import java.io.File
import java.io.FileOutputStream
import java.net.HttpURLConnection
import java.net.URL
import java.util.UUID
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.ensureActive
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.withContext
import kotlin.coroutines.coroutineContext

/** Owns local RetroArch Slang pack storage, official download and user archive import. */
class RetroArchShaderRepository(private val context: Context) {
    companion object {
        const val OFFICIAL_PACK_URL = "https://buildbot.libretro.com/assets/frontend/shaders_slang.zip"
        private const val MAX_ARCHIVE_BYTES = 256L * 1024 * 1024
        private const val PROGRESS_INTERVAL_NANOS = 100_000_000L
        private const val USER_AGENT = "EmuCoreV-Android"
        private val operationMutex = Mutex()
    }

    private val root: File get() = File(context.filesDir, "retroarch-shaders")
    private val store: RetroArchShaderPackStore get() = RetroArchShaderPackStore(root, context.cacheDir)

    suspend fun listPresets(): List<RetroArchShaderPreset> = withContext(Dispatchers.IO) { store.listPresets() }
    suspend fun hasInstalledOfficialPack(): Boolean = withContext(Dispatchers.IO) { store.hasInstalledPack() }
    suspend fun removePresets(paths: Set<String>): Result<Int> = withContext(Dispatchers.IO) { store.removePresets(paths) }

    /** Applies only a preset still present in the validated local catalog; null resets the chain. */
    suspend fun applyPreset(absolutePath: String?): Boolean = withContext(Dispatchers.IO) {
        if (!NativeRetroArchShaderBridge.isSupported()) return@withContext false
        val selected = if (absolutePath == null) {
            ""
        } else {
            store.listPresets().firstOrNull { it.absolutePath == absolutePath }?.absolutePath
                ?: return@withContext false
        }
        NativeRetroArchShaderBridge.setPreset(root.canonicalPath, selected)
    }

    fun setParameter(preset: RetroArchShaderPreset, parameterId: String, value: Float): Boolean {
        if (!NativeRetroArchShaderBridge.isSupported() || !value.isFinite()) return false
        val parameter = preset.parameters.firstOrNull { it.id == parameterId } ?: return false
        return NativeRetroArchShaderBridge.setParameter(
            parameter.id,
            value.coerceIn(parameter.minimum, parameter.maximum)
        )
    }

    fun resetParameters(): Boolean = NativeRetroArchShaderBridge.isSupported() &&
        NativeRetroArchShaderBridge.resetParameters()

    fun lastApplyError(): String? = NativeRetroArchShaderBridge.lastError()?.takeIf(String::isNotBlank)

    suspend fun downloadOfficialPack(
        onProgress: (RetroArchShaderInstallProgress) -> Unit = {}
    ): Result<Int> = withContext(Dispatchers.IO) {
        withOperationLock {
            runCatching {
                cleanupAbandonedArchives()
                val archive = File(context.cacheDir, "retroarch-shaders-${UUID.randomUUID()}.zip")
                try {
                    download(archive, onProgress)
                    coroutineContext.ensureActive()
                    onProgress(RetroArchShaderInstallProgress(RetroArchShaderInstallStage.INSTALLING))
                    store.installArchive(archive, official = true)
                } finally {
                    archive.delete()
                }
            }.onFailure { error ->
                if (error is kotlinx.coroutines.CancellationException) throw error
            }
        }
    }

    suspend fun importArchive(
        uri: Uri,
        onProgress: (RetroArchShaderInstallProgress) -> Unit = {}
    ): Result<Int> = withContext(Dispatchers.IO) {
        withOperationLock {
            runCatching {
                cleanupAbandonedArchives()
                val archive = File(context.cacheDir, "shader-import-${UUID.randomUUID()}.zip")
                try {
                    val input = context.contentResolver.openInputStream(uri) ?: error("Unable to open shader archive")
                    input.use { source ->
                        FileOutputStream(archive).use { output ->
                            val buffer = ByteArray(64 * 1024)
                            var bytes = 0L
                            var lastReportedAt = 0L
                            while (true) {
                                coroutineContext.ensureActive()
                                val read = source.read(buffer)
                                if (read < 0) break
                                bytes += read
                                require(bytes <= MAX_ARCHIVE_BYTES) { "Shader archive is too large" }
                                output.write(buffer, 0, read)
                                val now = System.nanoTime()
                                if (now - lastReportedAt >= PROGRESS_INTERVAL_NANOS) {
                                    onProgress(RetroArchShaderInstallProgress(RetroArchShaderInstallStage.DOWNLOADING, bytes))
                                    lastReportedAt = now
                                }
                            }
                            output.fd.sync()
                            onProgress(RetroArchShaderInstallProgress(RetroArchShaderInstallStage.DOWNLOADING, bytes, bytes))
                        }
                    }
                    coroutineContext.ensureActive()
                    onProgress(RetroArchShaderInstallProgress(RetroArchShaderInstallStage.INSTALLING))
                    store.installArchive(archive, official = false)
                } finally {
                    archive.delete()
                }
            }.onFailure { error ->
                if (error is kotlinx.coroutines.CancellationException) throw error
            }
        }
    }

    private suspend fun <T> withOperationLock(block: suspend () -> T): T {
        operationMutex.lock()
        try {
            return block()
        } finally {
            operationMutex.unlock()
        }
    }

    private fun cleanupAbandonedArchives() {
        context.cacheDir.listFiles().orEmpty()
            .filter { file ->
                file.isFile && file.name.endsWith(".zip") &&
                    (file.name.startsWith("retroarch-shaders-") || file.name.startsWith("shader-import-"))
            }
            .forEach { file -> check(file.delete() || !file.exists()) { "Unable to clear an abandoned shader archive" } }
    }

    private suspend fun download(
        archive: File,
        onProgress: (RetroArchShaderInstallProgress) -> Unit
    ) {
        var url = URL(OFFICIAL_PACK_URL)
        var redirectCount = 0
        while (true) {
            coroutineContext.ensureActive()
            require(url.protocol.equals("https", ignoreCase = true) &&
                (url.host.equals("buildbot.libretro.com", ignoreCase = true) ||
                    url.host.equals("assets.libretro.com", ignoreCase = true))) {
                "Official shader download redirected to an untrusted host"
            }
            val connection = (url.openConnection() as HttpURLConnection).apply {
                connectTimeout = 20_000
                readTimeout = 30_000
                instanceFollowRedirects = false
                setRequestProperty("User-Agent", USER_AGENT)
            }
            try {
                val status = connection.responseCode
                if (status in 300..399) {
                    require(redirectCount++ < 3) { "Too many shader download redirects" }
                    val location = connection.getHeaderField("Location") ?: error("Missing download redirect target")
                    url = URL(url, location)
                    continue
                }
                require(status in 200..299) { "Shader download failed with HTTP $status" }
                val expectedBytes = connection.contentLengthLong
                require(expectedBytes <= MAX_ARCHIVE_BYTES) { "Shader archive is too large" }
                onProgress(RetroArchShaderInstallProgress(RetroArchShaderInstallStage.DOWNLOADING, totalBytes = expectedBytes.coerceAtLeast(0)))
                connection.inputStream.use { input ->
                    FileOutputStream(archive).use { output ->
                        val buffer = ByteArray(64 * 1024)
                        var bytes = 0L
                        var lastReportedAt = 0L
                        while (true) {
                            coroutineContext.ensureActive()
                            val count = input.read(buffer)
                            if (count < 0) break
                            bytes += count
                            require(bytes <= MAX_ARCHIVE_BYTES) { "Shader archive is too large" }
                            output.write(buffer, 0, count)
                            val now = System.nanoTime()
                            if (now - lastReportedAt >= PROGRESS_INTERVAL_NANOS) {
                                onProgress(RetroArchShaderInstallProgress(RetroArchShaderInstallStage.DOWNLOADING, bytes, expectedBytes.coerceAtLeast(0)))
                                lastReportedAt = now
                            }
                        }
                        require(expectedBytes <= 0 || bytes == expectedBytes) { "Shader download is incomplete" }
                        output.fd.sync()
                        onProgress(RetroArchShaderInstallProgress(RetroArchShaderInstallStage.DOWNLOADING, bytes, expectedBytes.takeIf { it > 0 } ?: bytes))
                    }
                }
                return
            } finally {
                connection.disconnect()
            }
        }
    }
}

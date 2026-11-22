package com.sbro.emucorev.core

import android.content.Context
import android.os.Build
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Deferred
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.async
import kotlinx.coroutines.flow.StateFlow
import java.io.File

/** Last measured GL_RENDERER snapshot, valid only for the build/driver it was measured on. */
internal interface DeviceGpuPersistence {
    fun load(): DeviceGpuSnapshot?
    fun save(snapshot: DeviceGpuSnapshot)
}

/**
 * Plain-file store (not SharedPreferences, whose per-process cache goes stale across the app's
 * processes). [key] must change with the OS build / GPU driver; a mismatch is a miss.
 */
internal class DeviceGpuSnapshotFile(
    private val fileProvider: () -> File?,
    private val key: () -> String?
) : DeviceGpuPersistence {
    override fun load(): DeviceGpuSnapshot? = runCatching {
        val expected = key()?.takeIf(String::isNotBlank) ?: return null
        val file = fileProvider() ?: return null
        if (!file.isFile) return null
        val lines = file.readLines(Charsets.UTF_8)
        if (lines.size < 5 || lines[0] != FORMAT || lines[1] != expected) return null
        val vendor = runCatching { GpuVendor.valueOf(lines[2]) }.getOrNull() ?: return null
        val name = lines[3].takeIf(String::isNotBlank) ?: return null
        val renderer = lines[4].takeIf(String::isNotBlank) ?: return null
        DeviceGpuSnapshot(GpuModelInfo(vendor, name), renderer, GpuInfoSource.GL_RENDERER)
    }.getOrNull()

    override fun save(snapshot: DeviceGpuSnapshot) {
        val model = snapshot.model ?: return
        val renderer = snapshot.renderer ?: return
        if (snapshot.source != GpuInfoSource.GL_RENDERER) return
        val expected = key()?.takeIf(String::isNotBlank) ?: return
        val file = fileProvider() ?: return
        runCatching {
            fun clean(value: String) = value.filterNot { it == '\n' || it == '\r' }
            val tmp = File(file.parentFile, file.name + ".tmp")
            tmp.writeText(listOf(FORMAT, clean(expected), model.vendor.name, clean(model.displayName),
                clean(renderer)).joinToString("\n"), Charsets.UTF_8)
            if (!tmp.renameTo(file)) { file.delete(); tmp.renameTo(file) }
        }
    }

    private companion object { const val FORMAT = "emucorev-gpu-probe-1" }
}

/**
 * Single process-lifetime acquisition; cancellation of a UI waiter never cancels the shared probe.
 * A probe that returned no renderer (timeout/EGL failure) is "not measured": its fallback is
 * published but a later [ensureLoaded] retries after [retryBackoffMillis].
 */
internal class DeviceGpuInfoLoader(
    buildCpuName: () -> String,
    private val acquireCpuName: () -> String,
    private val queryRenderer: () -> String?,
    private val workerScope: CoroutineScope = CoroutineScope(SupervisorJob() + Dispatchers.IO),
    private val persistence: DeviceGpuPersistence? = null,
    private val retryBackoffMillis: Long = 30_000,
    private val clockMillis: () -> Long = { System.nanoTime() / 1_000_000 }
) {
    private val buildCpu by lazy(LazyThreadSafetyMode.PUBLICATION) { buildCpuName() }
    @Volatile private var resolvedCpu: String? = null
    private val cache = DeviceGpuSnapshotCache()
    private val loadMonitor = Any()
    private var loading: Deferred<DeviceGpuSnapshot>? = null
    private var retryNotBefore = 0L
    val snapshot: StateFlow<DeviceGpuSnapshot?> = cache.snapshot

    fun cpuName(): String = resolvedCpu ?: buildCpu

    /** True once a physical GL_RENDERER measurement (this process or a still-valid persisted one) is cached. */
    fun hasMeasuredSnapshot(): Boolean = cache.peek()?.source == GpuInfoSource.GL_RENDERER

    /** Build strings, memory and the persisted measurement (one small file read). No EGL, no waiting. */
    fun get(): DeviceGpuSnapshot = cache.getOrResolve {
        runCatching { persistence?.load() }.getOrNull()?.takeIf { it.source == GpuInfoSource.GL_RENDERER }
            ?: DeviceGpuInfoResolver.resolve(renderer = null, socName = cpuName())
    }

    suspend fun ensureLoaded(): DeviceGpuSnapshot {
        val task = synchronized(loadMonitor) {
            val current = loading
            if (current != null && current.isCompleted && clockMillis() >= retryNotBefore &&
                retryNotBefore != Long.MAX_VALUE
            ) loading = null
            loading ?: workerScope.async {
                val cpu = resolvedCpu ?: runCatching { acquireCpuName() }.getOrNull()
                    ?.takeIf(String::isNotBlank) ?: buildCpu
                val renderer = runCatching { queryRenderer() }.getOrNull()
                resolvedCpu = cpu
                val previous = cache.peek()
                val resolved = if (renderer == null && previous?.source == GpuInfoSource.GL_RENDERER) {
                    previous // a failed re-probe never downgrades a valid measurement
                } else DeviceGpuInfoResolver.resolve(renderer, cpu)
                synchronized(loadMonitor) {
                    retryNotBefore = if (renderer == null && resolved.source != GpuInfoSource.GL_RENDERER) {
                        clockMillis() + retryBackoffMillis
                    } else Long.MAX_VALUE
                }
                cache.publish(resolved)
                if (renderer != null && resolved.source == GpuInfoSource.GL_RENDERER) {
                    runCatching { persistence?.save(resolved) }
                }
                resolved
            }.also { loading = it }
        }
        return task.await()
    }
}

/** Device hardware acquisition only; active native renderer evidence never enters this provider. */
object DeviceGpuInfoProvider {
    private val loader = DeviceGpuInfoLoader(
        buildCpuName = MobileSocNameMapper::currentDeviceName,
        acquireCpuName = {
            MobileSocNameMapper.resolve(
                socModel = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) Build.SOC_MODEL else null,
                hardware = Build.HARDWARE,
                board = Build.BOARD,
                manufacturer = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) Build.SOC_MANUFACTURER else Build.MANUFACTURER,
                model = Build.MODEL,
                device = Build.DEVICE,
                product = Build.PRODUCT,
                additionalIdentifiers = AndroidSocInfo.readIdentifiers()
            )
        },
        queryRenderer = GpuRendererQuery::query,
        persistence = DeviceGpuSnapshotFile(
            fileProvider = { PersistedContext.file() },
            key = { PersistedContext.key() }
        )
    )
    val snapshot: StateFlow<DeviceGpuSnapshot?> = loader.snapshot

    fun get(context: Context): DeviceGpuSnapshot {
        PersistedContext.attach(context)
        return loader.get()
    }

    /** True when a GL_RENDERER measurement (fresh or persisted for this build) is already available. */
    fun hasMeasured(context: Context): Boolean {
        get(context)
        return loader.hasMeasuredSnapshot()
    }

    fun cpuName(): String = loader.cpuName()

    suspend fun ensureLoaded(context: Context): DeviceGpuSnapshot {
        PersistedContext.attach(context)
        return loader.ensureLoaded()
    }
}

/** Application context holder for the persisted measurement; the key invalidates on OS/driver change. */
private object PersistedContext {
    @Volatile private var app: Context? = null
    fun attach(context: Context) { if (app == null) app = context.applicationContext ?: context }
    fun file(): File? = app?.let { File(it.filesDir, "gpu_probe.txt") }
    fun key(): String? {
        val ctx = app ?: return null
        val vulkan = runCatching {
            ctx.packageManager.systemAvailableFeatures.firstOrNull { it.name == "android.hardware.vulkan.version" }?.version
        }.getOrNull()
        return "${Build.FINGERPRINT}|vk=$vulkan"
    }
}

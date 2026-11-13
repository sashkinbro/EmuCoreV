package com.sbro.emucorev.core

import android.content.Context
import android.os.Build
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Deferred
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.async
import kotlinx.coroutines.flow.StateFlow

/** Single process-lifetime acquisition; cancellation of a UI waiter never cancels the shared probe. */
internal class DeviceGpuInfoLoader(
    buildCpuName: () -> String,
    private val acquireCpuName: () -> String,
    private val queryRenderer: () -> String?,
    private val workerScope: CoroutineScope = CoroutineScope(SupervisorJob() + Dispatchers.IO)
) {
    private val buildCpu by lazy(LazyThreadSafetyMode.PUBLICATION) { buildCpuName() }
    @Volatile private var resolvedCpu: String? = null
    private val cache = DeviceGpuSnapshotCache()
    private val loadMonitor = Any()
    private var loading: Deferred<DeviceGpuSnapshot>? = null
    val snapshot: StateFlow<DeviceGpuSnapshot?> = cache.snapshot

    fun cpuName(): String = resolvedCpu ?: buildCpu

    /** Build strings and memory only. No preferences, file reads, EGL, or waiting for a probe. */
    fun get(): DeviceGpuSnapshot = cache.getOrResolve {
        DeviceGpuInfoResolver.resolve(renderer = null, socName = cpuName())
    }

    suspend fun ensureLoaded(): DeviceGpuSnapshot {
        val task = synchronized(loadMonitor) {
            loading ?: workerScope.async {
                val cpu = runCatching { acquireCpuName() }.getOrNull()
                    ?.takeIf(String::isNotBlank) ?: buildCpu
                val renderer = runCatching { queryRenderer() }.getOrNull()
                val resolved = DeviceGpuInfoResolver.resolve(renderer, cpu)
                resolvedCpu = cpu
                cache.publish(resolved)
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
        queryRenderer = GpuRendererQuery::query
    )
    val snapshot: StateFlow<DeviceGpuSnapshot?> = loader.snapshot

    @Suppress("UNUSED_PARAMETER")
    fun get(context: Context): DeviceGpuSnapshot = loader.get()

    fun cpuName(): String = loader.cpuName()

    @Suppress("UNUSED_PARAMETER")
    suspend fun ensureLoaded(context: Context): DeviceGpuSnapshot = loader.ensureLoaded()
}

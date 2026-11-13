package com.sbro.emucorev.core

/** Native session evidence and inferred physical hardware must remain distinguishable. */
internal enum class HardwareLabelOrigin { ACTIVE_RENDERER, DEVICE_HARDWARE }
internal data class PerformanceGpuLabel(val text: String, val origin: HardwareLabelOrigin, val source: GpuInfoSource)

/** Values acquired by callers; this helper never reads Android properties or files. */
internal data class CpuHardwareEvidence(
    val socModel: String?, val hardware: String? = null, val board: String? = null,
    val manufacturer: String? = null, val model: String? = null, val device: String? = null,
    val product: String? = null, val additionalIdentifiers: List<String> = emptyList()
)
internal object PerformanceHardwareLabels {
    fun cpu(evidence: CpuHardwareEvidence): String = MobileSocNameMapper.resolve(
        socModel = evidence.socModel,
        hardware = evidence.hardware,
        board = evidence.board,
        manufacturer = evidence.manufacturer,
        model = evidence.model,
        device = evidence.device,
        product = evidence.product,
        additionalIdentifiers = evidence.additionalIdentifiers
    )

    fun gpu(nativeRenderer: String?, device: DeviceGpuSnapshot?): PerformanceGpuLabel? {
        val raw = nativeRenderer?.trim()?.takeIf(String::isNotEmpty)
        if (raw != null) {
            val active = DeviceGpuInfoResolver.resolveActiveRenderer(raw)
            return PerformanceGpuLabel(
                active.model?.displayName ?: raw,
                HardwareLabelOrigin.ACTIVE_RENDERER,
                active.source
            )
        }
        val physicalModel = device?.model?.displayName?.takeIf(String::isNotBlank) ?: return null
        return PerformanceGpuLabel(physicalModel, HardwareLabelOrigin.DEVICE_HARDWARE, device.source)
    }
}

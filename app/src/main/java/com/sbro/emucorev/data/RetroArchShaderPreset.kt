package com.sbro.emucorev.data

data class RetroArchShaderPreset(
    val label: String,
    val absolutePath: String,
    val passCount: Int,
    val textureIds: List<String>,
    val parameters: List<RetroArchShaderParameter>
) {
    val parameterIds: List<String> get() = parameters.map { it.id }
}

data class RetroArchShaderParameter(
    val id: String,
    val description: String,
    val initialValue: Float,
    val minimum: Float,
    val maximum: Float,
    val step: Float
)

enum class RetroArchShaderInstallStage { DOWNLOADING, INSTALLING }

data class RetroArchShaderInstallProgress(
    val stage: RetroArchShaderInstallStage,
    val transferredBytes: Long = 0L,
    val totalBytes: Long = 0L
) {
    val fraction: Float?
        get() = totalBytes.takeIf { it > 0L }
            ?.let { (transferredBytes.toDouble() / it.toDouble()).toFloat().coerceIn(0f, 1f) }
}

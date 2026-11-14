package com.sbro.emucorev.data

data class RetroArchShaderPreset(
    val label: String,
    val absolutePath: String,
    val passCount: Int,
    val textureIds: List<String>,
    val parameters: List<RetroArchShaderParameter>
) {
    val parameterIds: List<String> get() = parameters.map { it.id }

    companion object { const val MAX_PARAMETERS = 4096 }
}

data class RetroArchShaderParameter(
    val id: String,
    val description: String,
    val initialValue: Float,
    val minimum: Float,
    val maximum: Float,
    val step: Float
)

enum class RetroArchShaderInstallStage { DOWNLOADING, INSTALLING, VALIDATING }

data class RetroArchShaderInstallProgress(
    val stage: RetroArchShaderInstallStage,
    val transferredBytes: Long = 0L,
    val totalBytes: Long = 0L,
    val bytesPerSecond: Long = 0L,
    val processedFiles: Int = 0,
    val totalFiles: Int = 0
) {
    val fraction: Float?
        get() = if (totalFiles > 0 && stage == RetroArchShaderInstallStage.VALIDATING) {
            (processedFiles.toFloat() / totalFiles).coerceIn(0f, 1f)
        } else totalBytes.takeIf { it > 0L }
            ?.let { (transferredBytes.toDouble() / it.toDouble()).toFloat().coerceIn(0f, 1f) }
}

package com.sbro.emucorev.ui.common

import androidx.annotation.StringRes
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.CheckCircle
import androidx.compose.material.icons.rounded.ErrorOutline
import androidx.compose.material.icons.rounded.RemoveCircleOutline
import androidx.compose.material.icons.rounded.WarningAmber
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.luminance
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import com.sbro.emucorev.R
import com.sbro.emucorev.data.EcvCompatibilityResult
import com.sbro.emucorev.data.EcvCompatibilityStatus
import com.sbro.emucorev.data.EcvCompatibilityUiRun
import com.sbro.emucorev.ui.theme.neon.neonPillShape
import com.sbro.emucorev.ui.theme.neon.neonShape

/** A run count describes available evidence; it never aggregates outcomes into Playable. */
@Composable
fun EcvCompatibilitySummaryChip(runs: List<EcvCompatibilityUiRun>, modifier: Modifier = Modifier) {
    Surface(
        modifier = modifier,
        shape = neonPillShape(),
        color = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.5f)
    ) {
        Text(
            text = if (runs.isEmpty()) stringResource(R.string.ecv_compatibility_not_tested)
                else stringResource(R.string.ecv_compatibility_run_count, runs.size),
            modifier = Modifier.padding(horizontal = 12.dp, vertical = 8.dp),
            style = MaterialTheme.typography.labelLarge,
            color = MaterialTheme.colorScheme.onSurfaceVariant
        )
    }
}

internal enum class EcvOutcomeRole { SUCCESS, ERROR, WARNING, NEUTRAL }

internal data class EcvOutcomePresentation(
    @StringRes val labelRes: Int,
    val icon: ImageVector,
    val role: EcvOutcomeRole
)

internal fun ecvOutcomePresentation(status: EcvCompatibilityStatus): EcvOutcomePresentation = when (status) {
    EcvCompatibilityStatus.PASSED -> EcvOutcomePresentation(R.string.ecv_compatibility_passed, Icons.Rounded.CheckCircle, EcvOutcomeRole.SUCCESS)
    EcvCompatibilityStatus.FAILED -> EcvOutcomePresentation(R.string.ecv_compatibility_failed, Icons.Rounded.ErrorOutline, EcvOutcomeRole.ERROR)
    EcvCompatibilityStatus.LIMITED -> EcvOutcomePresentation(R.string.ecv_compatibility_limited, Icons.Rounded.WarningAmber, EcvOutcomeRole.WARNING)
    EcvCompatibilityStatus.NOT_TESTED -> EcvOutcomePresentation(R.string.ecv_compatibility_unknown, Icons.Rounded.RemoveCircleOutline, EcvOutcomeRole.NEUTRAL)
}

internal data class EcvOutcomeRow(@StringRes val labelRes: Int, val result: EcvCompatibilityResult)

internal fun ecvOutcomeRows(row: EcvCompatibilityUiRun): List<EcvOutcomeRow> = with(row.run.results) {
    listOf(
        EcvOutcomeRow(R.string.ecv_compatibility_gameplay, gameplay),
        EcvOutcomeRow(R.string.ecv_compatibility_graphics, graphics),
        EcvOutcomeRow(R.string.ecv_compatibility_audio, audio),
        EcvOutcomeRow(R.string.ecv_compatibility_quick_save, quickSave),
        EcvOutcomeRow(R.string.ecv_compatibility_warm_load, warmLoad),
        EcvOutcomeRow(R.string.ecv_compatibility_cold_load, coldLoad)
    )
}

/** Each exact Title ID and device/core run stays separate, including untested outcomes. */
@Composable
fun EcvCompatibilityResults(runs: List<EcvCompatibilityUiRun>, modifier: Modifier = Modifier) {
    Column(modifier = modifier.fillMaxWidth(), verticalArrangement = Arrangement.spacedBy(12.dp)) {
        if (runs.isEmpty()) {
            Text(stringResource(R.string.ecv_compatibility_not_tested), style = MaterialTheme.typography.bodyMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant)
        }
        runs.forEach { row ->
            val run = row.run
            Surface(
                shape = neonShape(18.dp),
                color = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.22f),
                border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.45f))
            ) {
                Column(Modifier.fillMaxWidth().padding(14.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                    EcvEvidenceLine(R.string.ecv_compatibility_title_id, row.titleId, emphasized = true)
                    EcvEvidenceLine(R.string.ecv_compatibility_device, listOf(run.device.manufacturer, run.device.model).joinToString(" "))
                    run.device.soc?.let { EcvEvidenceLine(R.string.ecv_compatibility_soc, it) }
                    run.device.gpu?.let { EcvEvidenceLine(R.string.ecv_compatibility_gpu, it) }
                    EcvEvidenceLine(R.string.ecv_compatibility_app_version, run.appVersion)
                    EcvEvidenceLine(R.string.ecv_compatibility_core_version, run.coreVersion)
                    EcvEvidenceLine(R.string.ecv_compatibility_tested_on, run.testedOn.toString())
                    ecvOutcomeRows(row).forEach { outcome -> EcvResultRow(outcome) }
                    if (run.rendererSettings.isNotEmpty()) {
                        Text(stringResource(R.string.ecv_compatibility_settings), style = MaterialTheme.typography.labelLarge,
                            color = MaterialTheme.colorScheme.onSurface)
                        run.rendererSettings.forEach { (key, value) ->
                            Text("$key: $value", style = MaterialTheme.typography.bodySmall,
                                color = MaterialTheme.colorScheme.onSurfaceVariant)
                        }
                    }
                    run.notes?.let { EcvEvidenceLine(R.string.ecv_compatibility_notes, it) }
                }
            }
        }
    }
}

@Composable
private fun EcvEvidenceLine(@StringRes labelRes: Int, value: String, emphasized: Boolean = false) {
    Text(
        text = stringResource(labelRes, value),
        style = if (emphasized) MaterialTheme.typography.titleSmall else MaterialTheme.typography.bodySmall,
        fontWeight = if (emphasized) FontWeight.SemiBold else FontWeight.Normal,
        color = if (emphasized) MaterialTheme.colorScheme.onSurface else MaterialTheme.colorScheme.onSurfaceVariant
    )
}

@Composable
private fun EcvResultRow(outcome: EcvOutcomeRow) {
    val presentation = ecvOutcomePresentation(outcome.result.status)
    val dark = MaterialTheme.colorScheme.surface.luminance() < 0.5f
    val color = when (presentation.role) {
        EcvOutcomeRole.SUCCESS -> if (dark) Color(0xFF81C784) else Color(0xFF2E7D32)
        EcvOutcomeRole.ERROR -> MaterialTheme.colorScheme.error
        EcvOutcomeRole.WARNING -> if (dark) Color(0xFFFFD54F) else Color(0xFF8C6D00)
        EcvOutcomeRole.NEUTRAL -> MaterialTheme.colorScheme.onSurfaceVariant
    }
    Column(verticalArrangement = Arrangement.spacedBy(4.dp)) {
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            Icon(presentation.icon, contentDescription = null, tint = color, modifier = Modifier.size(18.dp))
            Column {
                Text(stringResource(outcome.labelRes), style = MaterialTheme.typography.labelLarge,
                    color = MaterialTheme.colorScheme.onSurface)
                Text(stringResource(presentation.labelRes), style = MaterialTheme.typography.bodySmall, color = color)
            }
        }
        outcome.result.evidence.forEach { EcvEvidenceLine(R.string.ecv_compatibility_evidence, it) }
    }
}

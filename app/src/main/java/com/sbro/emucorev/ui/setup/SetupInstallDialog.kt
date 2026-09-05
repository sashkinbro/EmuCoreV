package com.sbro.emucorev.ui.setup

import com.sbro.emucorev.ui.theme.neon.neonButtonShape
import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.animation.core.tween
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import com.sbro.emucorev.ui.theme.neon.neonShape

import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.CheckCircle
import androidx.compose.material.icons.rounded.ErrorOutline
import androidx.compose.material.icons.rounded.Inventory2
import androidx.compose.material.icons.rounded.SystemUpdateAlt
import androidx.compose.material.icons.rounded.VpnKey
import androidx.compose.material3.Button
import androidx.compose.material3.Icon
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.res.pluralStringResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.window.Dialog
import androidx.compose.ui.window.DialogProperties
import com.sbro.emucorev.R
import kotlin.math.roundToInt

@Composable
fun SetupInstallDialog(
    uiState: SetupInstallUiState,
    onDismiss: () -> Unit
) {
    if (!uiState.visible) return

    val operationIcon = when (uiState.operation) {
        InstallOperation.Firmware -> Icons.Rounded.SystemUpdateAlt
        InstallOperation.Content -> Icons.Rounded.Inventory2
        InstallOperation.License -> Icons.Rounded.VpnKey
        InstallOperation.Pkg -> Icons.Rounded.VpnKey
        null -> Icons.Rounded.Inventory2
    }
    val statusIcon = when (uiState.status) {
        InstallStatus.Running -> operationIcon
        InstallStatus.Success -> Icons.Rounded.CheckCircle
        InstallStatus.Error -> Icons.Rounded.ErrorOutline
        InstallStatus.Idle -> operationIcon
    }
    val title = when (uiState.operation) {
        InstallOperation.Firmware -> stringResource(R.string.install_dialog_title_firmware)
        InstallOperation.Content -> stringResource(R.string.install_dialog_title_content)
        InstallOperation.License -> stringResource(R.string.install_dialog_title_license)
        InstallOperation.Pkg -> stringResource(R.string.install_dialog_title_pkg)
        null -> stringResource(R.string.install_dialog_title_generic)
    }
    val itemProgress = if (uiState.status == InstallStatus.Running && uiState.current != null && uiState.total != null) {
        pluralStringResource(
            R.plurals.install_dialog_items_progress,
            uiState.total,
            uiState.current,
            uiState.total
        )
    } else {
        null
    }
    val percent = uiState.progress.roundToInt().coerceIn(0, 100)
    val animatedProgress by animateFloatAsState(
        targetValue = (uiState.progress / 100f).coerceIn(0f, 1f),
        animationSpec = tween(260),
        label = "setup-install-progress"
    )

    Dialog(
        onDismissRequest = {
            if (uiState.status != InstallStatus.Running) onDismiss()
        },
        properties = DialogProperties(
            usePlatformDefaultWidth = false,
            dismissOnBackPress = uiState.status != InstallStatus.Running,
            dismissOnClickOutside = uiState.status != InstallStatus.Running
        )
    ) {
        BoxWithConstraints(
            modifier = Modifier
                .fillMaxSize()
                .padding(horizontal = 20.dp, vertical = 16.dp),
            contentAlignment = Alignment.Center
        ) {
            Surface(
                modifier = Modifier
                    .fillMaxWidth()
                    .widthIn(max = 560.dp)
                    .heightIn(max = maxHeight),
                shape = neonShape(30.dp),
                color = MaterialTheme.colorScheme.surface,
                tonalElevation = 8.dp,
                shadowElevation = 16.dp,
                border = BorderStroke(
                    1.dp,
                    MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.58f)
                )
            ) {
                Column(
                    modifier = Modifier
                        .fillMaxWidth()
                        .verticalScroll(rememberScrollState())
                        .padding(22.dp),
                    verticalArrangement = Arrangement.spacedBy(18.dp)
                ) {
                    InstallDialogHeader(
                        icon = statusIcon,
                        status = uiState.status,
                        title = title
                    )

                    if (uiState.status == InstallStatus.Running) {
                        InstallProgressContent(
                            percent = percent,
                            progress = animatedProgress,
                            itemProgress = itemProgress,
                            detail = uiState.detail
                        )
                    } else {
                        InstallResultContent(
                            status = uiState.status,
                            message = when (uiState.status) {
                                InstallStatus.Success -> uiState.message ?: stringResource(R.string.install_dialog_success)
                                InstallStatus.Error -> uiState.message ?: stringResource(R.string.install_dialog_error)
                                else -> title
                            },
                            detail = uiState.detail
                        )
                        Button(
                            shape = neonButtonShape(),
                            onClick = onDismiss,
                            modifier = Modifier.fillMaxWidth()
                        ) {
                            Text(stringResource(R.string.install_dialog_close))
                        }
                    }
                }
            }
        }
    }
}

@Composable
private fun InstallDialogHeader(
    icon: ImageVector,
    status: InstallStatus,
    title: String
) {
    Row(
        modifier = Modifier.fillMaxWidth(),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(14.dp)
    ) {
        StatusIconChip(
            icon = icon,
            status = status
        )
        Column(modifier = Modifier.weight(1f)) {
            Text(
                text = title,
                style = MaterialTheme.typography.titleLarge.copy(fontWeight = FontWeight.Bold),
                color = MaterialTheme.colorScheme.onSurface
            )
        }
    }
}

@Composable
private fun InstallProgressContent(
    percent: Int,
    progress: Float,
    itemProgress: String?,
    detail: String?
) {
    Column(
        modifier = Modifier.fillMaxWidth(),
        verticalArrangement = Arrangement.spacedBy(14.dp),
        horizontalAlignment = Alignment.CenterHorizontally
    ) {
        Surface(
            modifier = Modifier.fillMaxWidth(),
            shape = neonShape(22.dp),
            color = MaterialTheme.colorScheme.surfaceContainerHigh,
            border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.42f))
        ) {
            Column(
                modifier = Modifier.padding(18.dp),
                verticalArrangement = Arrangement.spacedBy(10.dp),
                horizontalAlignment = Alignment.CenterHorizontally
            ) {
                LinearProgressIndicator(
                    progress = { progress },
                    modifier = Modifier
                        .fillMaxWidth()
                        .height(10.dp)
                        .clip(CircleShape),
                    trackColor = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.82f)
                )
                Text(
                    text = stringResource(R.string.install_dialog_percent, percent),
                    style = MaterialTheme.typography.labelLarge.copy(fontWeight = FontWeight.SemiBold),
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                    textAlign = TextAlign.Center,
                    modifier = Modifier.fillMaxWidth()
                )
            }
        }

        if (itemProgress != null || !detail.isNullOrBlank()) {
            Column(
                modifier = Modifier.fillMaxWidth(),
                verticalArrangement = Arrangement.spacedBy(8.dp)
            ) {
                itemProgress?.let {
                    InstallInfoRow(text = it)
                }
                detail?.takeIf { it.isNotBlank() }?.let {
                    InstallInfoRow(text = it)
                }
            }
        }
    }
}

@Composable
private fun InstallResultContent(
    status: InstallStatus,
    message: String,
    detail: String?
) {
    val isError = status == InstallStatus.Error
    Surface(
        modifier = Modifier.fillMaxWidth(),
        shape = neonShape(22.dp),
        color = if (isError) {
            MaterialTheme.colorScheme.errorContainer.copy(alpha = 0.58f)
        } else {
            MaterialTheme.colorScheme.primaryContainer.copy(alpha = 0.34f)
        },
        border = BorderStroke(
            1.dp,
            if (isError) {
                MaterialTheme.colorScheme.error.copy(alpha = 0.25f)
            } else {
                MaterialTheme.colorScheme.primary.copy(alpha = 0.16f)
            }
        )
    ) {
        Column(
            modifier = Modifier.padding(16.dp),
            verticalArrangement = Arrangement.spacedBy(10.dp),
            horizontalAlignment = Alignment.CenterHorizontally
        ) {
            Text(
                text = message,
                style = MaterialTheme.typography.bodyLarge.copy(fontWeight = FontWeight.SemiBold),
                color = if (isError) {
                    MaterialTheme.colorScheme.onErrorContainer
                } else {
                    MaterialTheme.colorScheme.onSurface
                },
                textAlign = TextAlign.Center,
                modifier = Modifier.fillMaxWidth()
            )
            detail?.takeIf { it.isNotBlank() }?.let {
                Text(
                    text = it,
                    style = MaterialTheme.typography.bodySmall,
                    color = if (isError) {
                        MaterialTheme.colorScheme.onErrorContainer
                    } else {
                        MaterialTheme.colorScheme.onSurfaceVariant
                    },
                    textAlign = TextAlign.Center,
                    modifier = Modifier.fillMaxWidth()
                )
            }
        }
    }
}

@Composable
private fun InstallInfoRow(text: String) {
    Surface(
        modifier = Modifier.fillMaxWidth(),
        shape = neonShape(16.dp),
        color = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.58f),
        border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.32f))
    ) {
        Text(
            text = text,
            modifier = Modifier.padding(horizontal = 14.dp, vertical = 10.dp),
            style = MaterialTheme.typography.bodySmall,
            color = MaterialTheme.colorScheme.onSurfaceVariant
        )
    }
}

@Composable
private fun StatusIconChip(
    icon: ImageVector,
    status: InstallStatus
) {
    val iconColor = statusTint(status)

    Box(
        modifier = Modifier
            .size(54.dp)
            .clip(neonShape(18.dp))
            .background(iconColor.copy(alpha = 0.13f)),
        contentAlignment = Alignment.Center
    ) {
        Icon(
            imageVector = icon,
            contentDescription = null,
            tint = iconColor,
            modifier = Modifier.size(28.dp)
        )
    }
}

@Composable
private fun statusTint(status: InstallStatus) = when (status) {
    InstallStatus.Success -> MaterialTheme.colorScheme.primary
    InstallStatus.Error -> MaterialTheme.colorScheme.error
    InstallStatus.Running -> MaterialTheme.colorScheme.secondary
    InstallStatus.Idle -> MaterialTheme.colorScheme.primary
}

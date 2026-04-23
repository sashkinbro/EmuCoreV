package com.sbro.emucorev.ui.setup

import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.BorderStroke
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.Icon
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.Alignment
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.CheckCircle
import androidx.compose.material.icons.rounded.Close
import androidx.compose.material.icons.rounded.ErrorOutline
import androidx.compose.material.icons.rounded.Inventory2
import androidx.compose.material.icons.rounded.SystemUpdateAlt
import androidx.compose.material.icons.rounded.VpnKey
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.res.pluralStringResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import com.sbro.emucorev.R

@Composable
fun SetupInstallDialog(
    uiState: SetupInstallUiState,
    onDismiss: () -> Unit
) {
    if (!uiState.visible) return

    val operationIcon = when (uiState.operation) {
        InstallOperation.Firmware -> Icons.Rounded.SystemUpdateAlt
        InstallOperation.Content -> Icons.Rounded.Inventory2
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
        InstallOperation.Pkg -> stringResource(R.string.install_dialog_title_pkg)
        null -> stringResource(R.string.install_dialog_title_generic)
    }

    val body = when (uiState.status) {
        InstallStatus.Running -> {
            if (uiState.current != null && uiState.total != null) {
                pluralStringResource(
                    R.plurals.install_dialog_items_progress,
                    uiState.total,
                    uiState.current,
                    uiState.total
                )
            } else {
                ""
            }
        }
        InstallStatus.Success -> uiState.message ?: stringResource(R.string.install_dialog_success)
        InstallStatus.Error -> listOfNotNull(
            uiState.message ?: stringResource(R.string.install_dialog_error),
            uiState.detail
        ).joinToString("\n\n")
        InstallStatus.Idle -> ""
    }

    AlertDialog(
        onDismissRequest = {
            if (uiState.status != InstallStatus.Running) {
                onDismiss()
            }
        },
        icon = {
            StatusIconChip(
                icon = statusIcon,
                status = uiState.status
            )
        },
        title = {
            Text(
                text = title,
                style = MaterialTheme.typography.titleLarge,
                color = MaterialTheme.colorScheme.onSurface
            )
        },
        text = {
            Column(verticalArrangement = Arrangement.spacedBy(12.dp)) {
                if (body.isNotBlank()) {
                    Text(
                        text = body,
                        style = MaterialTheme.typography.bodyMedium,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                        textAlign = TextAlign.Center,
                        modifier = Modifier.fillMaxWidth()
                    )
                }
                if (uiState.status == InstallStatus.Running) {
                    Surface(
                        shape = RoundedCornerShape(18.dp),
                        color = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.72f),
                        border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.45f))
                    ) {
                        Row(
                            modifier = Modifier
                                .fillMaxWidth()
                                .padding(horizontal = 14.dp, vertical = 12.dp),
                            horizontalArrangement = Arrangement.Center,
                            verticalAlignment = Alignment.CenterVertically
                        ) {
                            CircularProgressIndicator(
                                progress = { uiState.progress / 100f },
                                modifier = Modifier.size(20.dp),
                                strokeWidth = 2.5.dp
                            )
                            Text(
                                text = stringResource(
                                    R.string.install_dialog_percent,
                                    uiState.progress.toInt().coerceIn(0, 100)
                                ),
                                modifier = Modifier.padding(start = 12.dp),
                                style = MaterialTheme.typography.labelLarge,
                                color = MaterialTheme.colorScheme.onSurface,
                                textAlign = TextAlign.Center
                            )
                        }
                    }
                    LinearProgressIndicator(
                        progress = { uiState.progress / 100f },
                        modifier = Modifier
                            .fillMaxWidth()
                            .height(8.dp),
                        trackColor = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.8f)
                    )
                } else if (uiState.status == InstallStatus.Error && uiState.detail != null) {
                    Surface(
                        shape = RoundedCornerShape(18.dp),
                        color = MaterialTheme.colorScheme.errorContainer.copy(alpha = 0.62f),
                        border = BorderStroke(1.dp, MaterialTheme.colorScheme.error.copy(alpha = 0.28f))
                    ) {
                        Text(
                            text = uiState.detail,
                            modifier = Modifier.padding(14.dp),
                            style = MaterialTheme.typography.bodySmall,
                            color = MaterialTheme.colorScheme.onErrorContainer
                        )
                    }
                }
                Spacer(modifier = Modifier.height(2.dp))
            }
        },
        confirmButton = {
            if (uiState.status != InstallStatus.Running) {
                TextButton(onClick = onDismiss) {
                    Text(stringResource(R.string.install_dialog_close))
                }
            }
        },
        dismissButton = {},
        containerColor = MaterialTheme.colorScheme.surface,
        iconContentColor = MaterialTheme.colorScheme.onSurface,
        titleContentColor = MaterialTheme.colorScheme.onSurface,
        textContentColor = MaterialTheme.colorScheme.onSurfaceVariant
    )
}

@Composable
private fun StatusIconChip(
    icon: ImageVector,
    status: InstallStatus
) {
    val containerColor = when (status) {
        InstallStatus.Success -> MaterialTheme.colorScheme.primary.copy(alpha = 0.14f)
        InstallStatus.Error -> MaterialTheme.colorScheme.error.copy(alpha = 0.14f)
        InstallStatus.Running -> MaterialTheme.colorScheme.secondary.copy(alpha = 0.14f)
        InstallStatus.Idle -> MaterialTheme.colorScheme.primary.copy(alpha = 0.14f)
    }
    val iconColor = when (status) {
        InstallStatus.Success -> MaterialTheme.colorScheme.primary
        InstallStatus.Error -> MaterialTheme.colorScheme.error
        InstallStatus.Running -> MaterialTheme.colorScheme.secondary
        InstallStatus.Idle -> MaterialTheme.colorScheme.primary
    }

    Surface(
        shape = RoundedCornerShape(18.dp),
        color = containerColor,
        border = BorderStroke(1.dp, iconColor.copy(alpha = 0.16f))
    ) {
        Icon(
            imageVector = icon,
            contentDescription = null,
            tint = iconColor,
            modifier = Modifier.padding(14.dp)
        )
    }
}

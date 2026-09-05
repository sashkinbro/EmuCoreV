package com.sbro.emucorev.ui.settings

import com.sbro.emucorev.ui.theme.neon.neonButtonShape
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.rememberScrollState
import com.sbro.emucorev.ui.theme.neon.neonShape

import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.rounded.OpenInNew
import androidx.compose.material.icons.rounded.CloudDownload
import androidx.compose.material.icons.rounded.ErrorOutline
import androidx.compose.material.icons.rounded.NewReleases
import androidx.compose.material.icons.rounded.Refresh
import androidx.compose.material3.Button
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.platform.LocalUriHandler
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.window.Dialog
import androidx.compose.ui.window.DialogProperties
import com.sbro.emucorev.R
import com.sbro.emucorev.core.AppUpdateRelease
import com.sbro.emucorev.ui.theme.ScreenHorizontalPadding
import java.time.Instant
import java.time.ZoneId
import java.time.format.DateTimeFormatter
import java.util.Locale

@Composable
fun AppUpdateTab(
    state: AppUpdateUiState,
    onLoadReleaseHistory: (forceRefresh: Boolean) -> Unit
) {
    var selectedRelease by remember { mutableStateOf<AppUpdateRelease?>(null) }
    LaunchedEffect(Unit) {
        if (state.releaseHistory.isEmpty() && !state.historyLoading) {
            onLoadReleaseHistory(false)
        }
    }

    Surface(
        modifier = Modifier
            .fillMaxWidth()
            .padding(horizontal = ScreenHorizontalPadding),
        shape = neonShape(24.dp),
        color = MaterialTheme.colorScheme.surface,
        tonalElevation = 3.dp,
        border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant)
    ) {
        Column(
            modifier = Modifier.padding(16.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            Row(
                modifier = Modifier.fillMaxWidth(),
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(12.dp)
            ) {
                Icon(
                    Icons.Rounded.NewReleases,
                    contentDescription = null,
                    tint = MaterialTheme.colorScheme.primary,
                    modifier = Modifier.size(24.dp)
                )
                Column(modifier = Modifier.weight(1f)) {
                    Text(
                        stringResource(R.string.settings_updates_history_title),
                        style = MaterialTheme.typography.titleMedium,
                        fontWeight = FontWeight.Bold
                    )
                    Text(
                        stringResource(R.string.settings_updates_history_body),
                        style = MaterialTheme.typography.bodyMedium,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                }
                OutlinedButton(
                    shape = neonButtonShape(),
                    onClick = { onLoadReleaseHistory(true) },
                    enabled = !state.historyLoading
                ) {
                    if (state.historyLoading) {
                        CircularProgressIndicator(modifier = Modifier.size(18.dp), strokeWidth = 2.dp)
                    } else {
                        Icon(Icons.Rounded.Refresh, contentDescription = null, modifier = Modifier.size(18.dp))
                    }
                }
            }

            when {
                state.historyLoading && state.releaseHistory.isEmpty() -> Text(
                    stringResource(R.string.settings_updates_history_loading),
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
                state.historyErrorMessage != null && state.releaseHistory.isEmpty() -> Row(
                    horizontalArrangement = Arrangement.spacedBy(8.dp),
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    Icon(Icons.Rounded.ErrorOutline, contentDescription = null, tint = MaterialTheme.colorScheme.error)
                    Text(state.historyErrorMessage, color = MaterialTheme.colorScheme.error)
                }
                state.releaseHistory.isEmpty() -> Text(
                    stringResource(R.string.settings_updates_history_empty),
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
                else -> state.releaseHistory.forEach { release ->
                    Surface(
                        modifier = Modifier.fillMaxWidth(),
                        shape = neonShape(18.dp),
                        color = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.35f),
                        onClick = { selectedRelease = release }
                    ) {
                        Column(
                            modifier = Modifier.padding(14.dp),
                            verticalArrangement = Arrangement.spacedBy(3.dp)
                        ) {
                            Text(
                                release.displayName,
                                fontWeight = FontWeight.SemiBold,
                                maxLines = 1,
                                overflow = TextOverflow.Ellipsis
                            )
                            Text(
                                releaseHistorySummary(release),
                                style = MaterialTheme.typography.bodySmall,
                                color = MaterialTheme.colorScheme.onSurfaceVariant
                            )
                        }
                    }
                }
            }
        }
    }

    selectedRelease?.let { release ->
        ReleaseDetailsDialog(release = release, onDismiss = { selectedRelease = null })
    }
}

@Composable
private fun ReleaseDetailsDialog(release: AppUpdateRelease, onDismiss: () -> Unit) {
    val uriHandler = LocalUriHandler.current
    Dialog(
        onDismissRequest = onDismiss,
        properties = DialogProperties(usePlatformDefaultWidth = false)
    ) {
        BoxWithConstraints(
            modifier = Modifier
                .fillMaxSize()
                .padding(horizontal = 18.dp, vertical = 16.dp),
            contentAlignment = Alignment.Center
        ) {
            val scrollState = rememberScrollState()
            Surface(
                modifier = Modifier
                    .fillMaxWidth()
                    .widthIn(max = 620.dp)
                    .heightIn(max = maxHeight),
                shape = neonShape(28.dp),
                color = MaterialTheme.colorScheme.surface,
                tonalElevation = 6.dp,
                shadowElevation = 12.dp,
                border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.62f))
            ) {
                Column(
                    modifier = Modifier
                        .verticalScroll(scrollState)
                        .padding(20.dp),
                    verticalArrangement = Arrangement.spacedBy(16.dp)
                ) {
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        verticalAlignment = Alignment.CenterVertically,
                        horizontalArrangement = Arrangement.spacedBy(14.dp)
                    ) {
                        Box(
                            modifier = Modifier
                                .size(48.dp)
                                .background(
                                    color = MaterialTheme.colorScheme.primary.copy(alpha = 0.12f),
                                    shape = neonShape(16.dp)
                                ),
                            contentAlignment = Alignment.Center
                        ) {
                            Icon(
                                imageVector = Icons.Rounded.NewReleases,
                                contentDescription = null,
                                tint = MaterialTheme.colorScheme.primary,
                                modifier = Modifier.size(26.dp)
                            )
                        }
                        Column(modifier = Modifier.weight(1f)) {
                            Text(
                                text = stringResource(R.string.settings_updates_history_title),
                                style = MaterialTheme.typography.labelLarge,
                                fontWeight = FontWeight.SemiBold,
                                color = MaterialTheme.colorScheme.primary
                            )
                            Text(
                                text = release.displayName,
                                style = MaterialTheme.typography.titleLarge.copy(fontWeight = FontWeight.Bold),
                                color = MaterialTheme.colorScheme.onSurface,
                                maxLines = 2,
                                overflow = TextOverflow.Ellipsis
                            )
                        }
                    }

                    HorizontalDivider(color = MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.5f))

                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.spacedBy(10.dp)
                    ) {
                        UpdateMetric(
                            label = stringResource(R.string.settings_updates_version_label),
                            value = release.tagName.ifBlank { release.displayName },
                            modifier = Modifier.weight(1f)
                        )
                        UpdateMetric(
                            label = stringResource(R.string.settings_updates_published_label),
                            value = formatReleaseDate(release.publishedAt),
                            modifier = Modifier.weight(1f)
                        )
                    }

                    UpdateDialogSection(
                        title = stringResource(R.string.settings_updates_release_notes_title)
                    ) {
                        Text(
                            text = displayReleaseNotes(release.body)
                                .ifBlank { stringResource(R.string.settings_updates_release_notes_empty) },
                            style = MaterialTheme.typography.bodyMedium,
                            color = MaterialTheme.colorScheme.onSurfaceVariant
                        )
                    }

                    UpdateDialogSection(
                        title = stringResource(R.string.settings_updates_parallel_title)
                    ) {
                        UpdateInfoRow(
                            icon = Icons.Rounded.CloudDownload,
                            title = if (release.hasParallelApk) {
                                stringResource(R.string.settings_updates_parallel_package)
                            } else {
                                stringResource(R.string.settings_updates_parallel_unavailable)
                            },
                            body = if (release.hasParallelApk) {
                                formatBytes(release.parallelApkSizeBytes)
                            } else {
                                stringResource(R.string.settings_updates_parallel_body)
                            }
                        )
                        Button(
                            shape = neonButtonShape(),
                            onClick = {
                                release.parallelApkDownloadUrl
                                    ?.takeIf(String::isNotBlank)
                                    ?.let(uriHandler::openUri)
                            },
                            enabled = release.hasParallelApk,
                            modifier = Modifier.fillMaxWidth()
                        ) {
                            Icon(
                                imageVector = Icons.Rounded.CloudDownload,
                                contentDescription = null,
                                modifier = Modifier.size(18.dp)
                            )
                            Text(
                                text = stringResource(R.string.settings_updates_parallel_download),
                                modifier = Modifier.padding(start = 8.dp)
                            )
                        }
                    }

                    Button(
                        shape = neonButtonShape(),
                        onClick = {
                            release.htmlUrl.takeIf(String::isNotBlank)?.let(uriHandler::openUri)
                            onDismiss()
                        },
                        modifier = Modifier.fillMaxWidth()
                    ) {
                        Icon(
                            Icons.AutoMirrored.Rounded.OpenInNew,
                            contentDescription = null,
                            modifier = Modifier.size(18.dp)
                        )
                        Text(
                            stringResource(R.string.settings_updates_open_release),
                            modifier = Modifier.padding(start = 8.dp)
                        )
                    }

                    TextButton(
                        onClick = onDismiss,
                        modifier = Modifier.fillMaxWidth()
                    ) {
                        Text(stringResource(R.string.common_close))
                    }
                }
            }
        }
    }
}

@Composable
private fun UpdateMetric(label: String, value: String, modifier: Modifier = Modifier) {
    Surface(
        modifier = modifier,
        shape = neonShape(18.dp),
        color = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.24f),
        border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.62f))
    ) {
        Column(modifier = Modifier.padding(12.dp)) {
            Text(
                text = label,
                style = MaterialTheme.typography.labelMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
            Text(
                text = value,
                style = MaterialTheme.typography.titleMedium.copy(fontWeight = FontWeight.SemiBold),
                color = MaterialTheme.colorScheme.onSurface,
                maxLines = 2,
                overflow = TextOverflow.Ellipsis
            )
        }
    }
}

@Composable
private fun UpdateInfoRow(icon: ImageVector, title: String, body: String) {
    Row(
        modifier = Modifier.fillMaxWidth(),
        horizontalArrangement = Arrangement.spacedBy(12.dp),
        verticalAlignment = Alignment.Top
    ) {
        Icon(
            imageVector = icon,
            contentDescription = null,
            tint = MaterialTheme.colorScheme.primary,
            modifier = Modifier
                .padding(top = 2.dp)
                .size(22.dp)
        )
        Column(modifier = Modifier.weight(1f)) {
            Text(
                text = title,
                style = MaterialTheme.typography.bodyLarge.copy(fontWeight = FontWeight.SemiBold),
                color = MaterialTheme.colorScheme.onSurface
            )
            Text(
                text = body,
                style = MaterialTheme.typography.bodyMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
        }
    }
}

@Composable
private fun UpdateDialogSection(
    title: String,
    content: @Composable ColumnScope.() -> Unit
) {
    Surface(
        modifier = Modifier.fillMaxWidth(),
        shape = neonShape(20.dp),
        color = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.24f),
        border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.62f))
    ) {
        Column(
            modifier = Modifier.padding(PaddingValues(14.dp)),
            verticalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            Text(
                text = title,
                style = MaterialTheme.typography.titleMedium.copy(fontWeight = FontWeight.Bold),
                color = MaterialTheme.colorScheme.onSurface
            )
            content()
        }
    }
}

private fun formatBytes(bytes: Long?): String {
    bytes ?: return "-"
    if (bytes < 1024L) return "$bytes B"
    val units = listOf("KB", "MB", "GB")
    var value = bytes.toDouble() / 1024.0
    var unitIndex = 0
    while (value >= 1024.0 && unitIndex < units.lastIndex) {
        value /= 1024.0
        unitIndex++
    }
    return String.format(Locale.US, "%.1f %s", value, units[unitIndex])
}

private fun releaseHistorySummary(release: AppUpdateRelease): String = listOfNotNull(
    release.tagName.takeIf(String::isNotBlank),
    formatReleaseDate(release.publishedAt).takeIf { it != "-" },
    formatBytes(release.parallelApkSizeBytes).takeIf { release.hasParallelApk && it != "-" }
).joinToString(" • ")

private fun formatReleaseDate(value: String): String {
    if (value.isBlank()) return "-"
    return runCatching {
        Instant.parse(value)
            .atZone(ZoneId.systemDefault())
            .format(DateTimeFormatter.ofPattern("MMM d, yyyy", Locale.getDefault()))
    }.getOrDefault(value.substringBefore('T').ifBlank { "-" })
}

private fun displayReleaseNotes(body: String): String = body
    .replace("\r\n", "\n")
    .lines()
    .dropWhile { line ->
        val trimmed = line.trim()
        trimmed.isBlank() ||
            trimmed.contains("Full Changelog", ignoreCase = true) ||
            (trimmed.contains("github.com/", ignoreCase = true) && trimmed.contains("/compare/", ignoreCase = true))
    }
    .joinToString("\n")
    .replace("**", "")
    .trim()

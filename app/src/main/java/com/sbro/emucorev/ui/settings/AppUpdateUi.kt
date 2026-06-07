package com.sbro.emucorev.ui.settings

import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.core.RepeatMode
import androidx.compose.animation.core.animateFloat
import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.animation.core.infiniteRepeatable
import androidx.compose.animation.core.rememberInfiniteTransition
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
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.rounded.OpenInNew
import androidx.compose.material.icons.rounded.CheckCircle
import androidx.compose.material.icons.rounded.CloudDownload
import androidx.compose.material.icons.rounded.ErrorOutline
import androidx.compose.material.icons.rounded.InstallMobile
import androidx.compose.material.icons.rounded.KeyboardArrowDown
import androidx.compose.material.icons.rounded.KeyboardArrowUp
import androidx.compose.material.icons.rounded.NewReleases
import androidx.compose.material.icons.rounded.Refresh
import androidx.compose.material.icons.rounded.SystemUpdateAlt
import androidx.compose.material3.Button
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.LinearProgressIndicator
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
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.graphicsLayer
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
import com.sbro.emucorev.ui.common.SectionCard
import java.time.Instant
import java.time.ZoneId
import java.time.format.DateTimeFormatter
import java.util.Locale

@Composable
fun AppUpdateTab(
    state: AppUpdateUiState,
    onCheckForUpdates: () -> Unit,
    onLoadReleaseHistory: () -> Unit,
    onShowCleanInstallDialog: () -> Unit,
    onDismissCleanInstallDialog: () -> Unit,
    onDownloadUpdate: () -> Unit,
    onInstallDownloadedUpdate: () -> Unit,
    onDownloadParallelRelease: (AppUpdateRelease) -> Unit,
    onInstallDownloadedParallelRelease: (AppUpdateRelease) -> Unit
) {
    val uriHandler = LocalUriHandler.current
    val release = state.latestRelease
    var selectedHistoryRelease by remember { mutableStateOf<AppUpdateRelease?>(null) }

    LaunchedEffect(Unit) {
        if (!state.checkedOnce && !state.checking) {
            onCheckForUpdates()
        }
        if (state.releaseHistory.isEmpty() && !state.historyLoading) {
            onLoadReleaseHistory()
        }
    }

    Column(verticalArrangement = Arrangement.spacedBy(16.dp)) {
        UpdateHeroCard(
            state = state,
            onCheckForUpdates = onCheckForUpdates,
            onDownloadUpdate = onShowCleanInstallDialog,
            onInstallDownloadedUpdate = onShowCleanInstallDialog,
            onOpenRelease = {
                release?.htmlUrl?.takeIf(String::isNotBlank)?.let(uriHandler::openUri)
            }
        )

        if (release != null) {
            SectionCard(
                title = stringResource(R.string.settings_updates_release_notes_title),
                contentPadding = androidx.compose.foundation.layout.PaddingValues(16.dp)
            ) {
                Text(
                    text = displayReleaseNotes(release.body)
                        .ifBlank { stringResource(R.string.settings_updates_release_notes_empty) },
                    style = MaterialTheme.typography.bodyMedium,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
            }
        }

        ReleaseHistoryCard(
            releases = state.releaseHistory,
            loading = state.historyLoading,
            errorMessage = state.historyErrorMessage,
            onRetry = onLoadReleaseHistory,
            onReleaseClick = { selectedHistoryRelease = it }
        )

        SectionCard(
            title = stringResource(R.string.settings_updates_installation_title),
            contentPadding = androidx.compose.foundation.layout.PaddingValues(16.dp)
        ) {
            UpdateInfoRow(
                icon = Icons.Rounded.InstallMobile,
                title = stringResource(R.string.settings_updates_clean_install_title),
                body = stringResource(R.string.settings_updates_clean_install_body)
            )
            UpdateInfoRow(
                icon = Icons.Rounded.CloudDownload,
                title = stringResource(R.string.settings_updates_source_title),
                body = stringResource(R.string.settings_updates_source_body)
            )
            UpdateInfoRow(
                icon = Icons.Rounded.InstallMobile,
                title = stringResource(R.string.settings_updates_parallel_title),
                body = stringResource(R.string.settings_updates_parallel_body)
            )
        }
    }

    if (state.cleanInstallDialogVisible) {
        CleanInstallDialog(
            release = release,
            downloaded = state.downloadedApkPath != null,
            onDismiss = onDismissCleanInstallDialog,
            onConfirm = {
                onDismissCleanInstallDialog()
                if (state.downloadedApkPath != null) {
                    onInstallDownloadedUpdate()
                } else {
                    onDownloadUpdate()
                }
            }
        )
    }

    selectedHistoryRelease?.let { historyRelease ->
        ReleaseHistoryDialog(
            release = historyRelease,
            parallelDownloadProgress = state.parallelDownloadProgress[historyRelease.updateKey()],
            parallelDownloaded = state.downloadedParallelApkPaths.containsKey(historyRelease.updateKey()),
            onDismiss = { selectedHistoryRelease = null },
            onOpenRelease = {
                selectedHistoryRelease = null
                historyRelease.htmlUrl.takeIf(String::isNotBlank)?.let(uriHandler::openUri)
            },
            onDownloadParallelRelease = { onDownloadParallelRelease(historyRelease) },
            onInstallDownloadedParallelRelease = { onInstallDownloadedParallelRelease(historyRelease) }
        )
    }
}

@Composable
fun AppUpdateAvailableDialog(
    release: AppUpdateRelease,
    onDismiss: () -> Unit,
    onSkipUpdate: () -> Unit,
    onOpenUpdates: () -> Unit
) {
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
                    .widthIn(max = 560.dp)
                    .heightIn(max = maxHeight),
                shape = RoundedCornerShape(28.dp),
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
                                    shape = RoundedCornerShape(16.dp)
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
                                text = stringResource(R.string.settings_updates_dialog_title, release.displayName),
                                style = MaterialTheme.typography.titleLarge.copy(fontWeight = FontWeight.Bold),
                                color = MaterialTheme.colorScheme.onSurface
                            )
                        }
                    }

                    Text(
                        text = stringResource(R.string.settings_updates_dialog_body),
                        style = MaterialTheme.typography.bodyLarge,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )

                    Surface(
                        shape = RoundedCornerShape(18.dp),
                        color = MaterialTheme.colorScheme.surfaceContainerHigh
                    ) {
                        Row(
                            modifier = Modifier
                                .fillMaxWidth()
                                .padding(12.dp),
                            verticalAlignment = Alignment.CenterVertically,
                            horizontalArrangement = Arrangement.spacedBy(10.dp)
                        ) {
                            Icon(
                                imageVector = Icons.Rounded.CloudDownload,
                                contentDescription = null,
                                tint = MaterialTheme.colorScheme.primary,
                                modifier = Modifier.size(22.dp)
                            )
                            Column(modifier = Modifier.weight(1f)) {
                                Text(
                                    text = stringResource(R.string.settings_updates_dialog_download_label),
                                    style = MaterialTheme.typography.bodyLarge.copy(fontWeight = FontWeight.SemiBold),
                                    color = MaterialTheme.colorScheme.onSurface
                                )
                                Text(
                                    text = formatBytes(release.apkSizeBytes),
                                    style = MaterialTheme.typography.bodyMedium,
                                    color = MaterialTheme.colorScheme.onSurfaceVariant
                                )
                            }
                        }
                    }

                    Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                        Button(
                            onClick = onOpenUpdates,
                            modifier = Modifier.fillMaxWidth()
                        ) {
                            Text(stringResource(R.string.settings_updates_dialog_details))
                        }
                        Row(
                            modifier = Modifier.fillMaxWidth(),
                            horizontalArrangement = Arrangement.spacedBy(8.dp),
                            verticalAlignment = Alignment.CenterVertically
                        ) {
                            TextButton(
                                onClick = onDismiss,
                                modifier = Modifier.weight(1f)
                            ) {
                                Text(stringResource(R.string.settings_updates_dialog_later))
                            }
                            TextButton(
                                onClick = onSkipUpdate,
                                modifier = Modifier.weight(1.25f)
                            ) {
                                Text(stringResource(R.string.settings_updates_dialog_skip))
                            }
                        }
                    }
                }
            }
        }
    }
}

@Composable
private fun UpdateHeroCard(
    state: AppUpdateUiState,
    onCheckForUpdates: () -> Unit,
    onDownloadUpdate: () -> Unit,
    onInstallDownloadedUpdate: () -> Unit,
    onOpenRelease: () -> Unit
) {
    val release = state.latestRelease
    val progress = state.downloadProgress
    val progressValue by animateFloatAsState(targetValue = progress ?: 0f, label = "update-progress")
    val pulse = rememberInfiniteTransition(label = "update-pulse")
    val pulseScale by pulse.animateFloat(
        initialValue = 0.96f,
        targetValue = 1.04f,
        animationSpec = infiniteRepeatable(animation = tween(1200), repeatMode = RepeatMode.Reverse),
        label = "update-pulse-scale"
    )

    Surface(
        modifier = Modifier
            .fillMaxWidth()
            .padding(horizontal = 12.dp),
        shape = RoundedCornerShape(24.dp),
        color = MaterialTheme.colorScheme.surface,
        tonalElevation = 4.dp,
        shadowElevation = 8.dp,
        border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.65f))
    ) {
        Column(
            modifier = Modifier.padding(18.dp),
            verticalArrangement = Arrangement.spacedBy(16.dp)
        ) {
            Row(
                modifier = Modifier.fillMaxWidth(),
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(14.dp)
            ) {
                Box(
                    modifier = Modifier
                        .size(54.dp)
                        .graphicsLayer {
                            scaleX = if (state.checking || progress != null) pulseScale else 1f
                            scaleY = if (state.checking || progress != null) pulseScale else 1f
                        }
                        .background(
                            color = MaterialTheme.colorScheme.primary.copy(alpha = 0.12f),
                            shape = RoundedCornerShape(18.dp)
                        ),
                    contentAlignment = Alignment.Center
                ) {
                    Icon(
                        imageVector = when {
                            state.errorMessage != null -> Icons.Rounded.ErrorOutline
                            release == null -> Icons.Rounded.CheckCircle
                            else -> Icons.Rounded.SystemUpdateAlt
                        },
                        contentDescription = null,
                        tint = when {
                            state.errorMessage != null -> MaterialTheme.colorScheme.error
                            release == null -> MaterialTheme.colorScheme.tertiary
                            else -> MaterialTheme.colorScheme.primary
                        },
                        modifier = Modifier.size(28.dp)
                    )
                }
                Column(modifier = Modifier.weight(1f)) {
                    Text(
                        text = when {
                            state.checking -> stringResource(R.string.settings_updates_checking_title)
                            release != null -> stringResource(R.string.settings_updates_available_title)
                            state.errorMessage != null -> stringResource(R.string.settings_updates_error_title)
                            else -> stringResource(R.string.settings_updates_current_title)
                        },
                        style = MaterialTheme.typography.titleLarge.copy(fontWeight = FontWeight.Bold),
                        color = MaterialTheme.colorScheme.onSurface,
                        maxLines = 2,
                        overflow = TextOverflow.Ellipsis
                    )
                    Text(
                        text = when {
                            release != null -> release.displayName
                            state.errorMessage != null -> state.errorMessage
                            else -> stringResource(R.string.settings_updates_current_body)
                        },
                        style = MaterialTheme.typography.bodyMedium,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                }
            }

            if (release != null) {
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
                        label = stringResource(R.string.settings_updates_size_label),
                        value = formatBytes(release.apkSizeBytes),
                        modifier = Modifier.weight(1f)
                    )
                }
            }

            AnimatedVisibility(visible = progress != null) {
                Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                    LinearProgressIndicator(
                        progress = { progressValue.coerceIn(0f, 1f) },
                        modifier = Modifier.fillMaxWidth()
                    )
                    Text(
                        text = stringResource(R.string.settings_updates_downloading, (progressValue * 100).toInt().coerceIn(0, 100)),
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.primary
                    )
                }
            }

            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.spacedBy(10.dp)
            ) {
                OutlinedButton(
                    onClick = onCheckForUpdates,
                    enabled = !state.checking && progress == null,
                    modifier = Modifier.weight(1f)
                ) {
                    Icon(Icons.Rounded.Refresh, contentDescription = null, modifier = Modifier.size(18.dp))
                    Text(
                        text = stringResource(R.string.settings_updates_check_now),
                        modifier = Modifier.padding(start = 6.dp)
                    )
                }
                if (release != null) {
                    Button(
                        onClick = if (state.downloadedApkPath != null) onInstallDownloadedUpdate else onDownloadUpdate,
                        enabled = progress == null && release.hasInstallableApk,
                        modifier = Modifier.weight(1f)
                    ) {
                        Icon(
                            imageVector = if (state.downloadedApkPath != null) Icons.Rounded.InstallMobile else Icons.Rounded.CloudDownload,
                            contentDescription = null,
                            modifier = Modifier.size(18.dp)
                        )
                        Text(
                            text = stringResource(
                                if (state.downloadedApkPath != null) R.string.settings_updates_install
                                else R.string.settings_updates_download
                            ),
                            modifier = Modifier.padding(start = 6.dp)
                        )
                    }
                }
            }

            if (release != null) {
                TextButton(onClick = onOpenRelease, modifier = Modifier.align(Alignment.End)) {
                    Icon(Icons.AutoMirrored.Rounded.OpenInNew, contentDescription = null, modifier = Modifier.size(18.dp))
                    Text(
                        text = stringResource(R.string.settings_updates_open_release),
                        modifier = Modifier.padding(start = 6.dp)
                    )
                }
            }
        }
    }
}

@Composable
private fun UpdateMetric(label: String, value: String, modifier: Modifier = Modifier) {
    Surface(
        modifier = modifier,
        shape = RoundedCornerShape(18.dp),
        color = MaterialTheme.colorScheme.surfaceContainerHigh
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
                color = MaterialTheme.colorScheme.onSurface
            )
        }
    }
}

@Composable
private fun ReleaseHistoryCard(
    releases: List<AppUpdateRelease>,
    loading: Boolean,
    errorMessage: String?,
    onRetry: () -> Unit,
    onReleaseClick: (AppUpdateRelease) -> Unit
) {
    var expanded by rememberSaveable { mutableStateOf(false) }
    SectionCard(
        title = stringResource(R.string.settings_updates_history_title),
        contentPadding = androidx.compose.foundation.layout.PaddingValues(16.dp)
    ) {
        Column(verticalArrangement = Arrangement.spacedBy(12.dp)) {
            Surface(
                modifier = Modifier.fillMaxWidth(),
                shape = RoundedCornerShape(18.dp),
                color = MaterialTheme.colorScheme.surfaceContainerHigh,
                tonalElevation = 1.dp,
                shadowElevation = 2.dp,
                border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.55f)),
                onClick = { expanded = !expanded }
            ) {
                Row(
                    modifier = Modifier.padding(12.dp),
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.spacedBy(12.dp)
                ) {
                    Box(
                        modifier = Modifier
                            .size(40.dp)
                            .background(
                                color = MaterialTheme.colorScheme.primary.copy(alpha = 0.10f),
                                shape = RoundedCornerShape(14.dp)
                            ),
                        contentAlignment = Alignment.Center
                    ) {
                        Icon(
                            imageVector = Icons.Rounded.NewReleases,
                            contentDescription = null,
                            tint = MaterialTheme.colorScheme.primary,
                            modifier = Modifier.size(21.dp)
                        )
                    }
                    Column(
                        modifier = Modifier.weight(1f),
                        verticalArrangement = Arrangement.spacedBy(3.dp)
                    ) {
                        Text(
                            text = stringResource(R.string.settings_updates_history_body),
                            style = MaterialTheme.typography.bodyMedium,
                            color = MaterialTheme.colorScheme.onSurfaceVariant
                        )
                    }
                    Icon(
                        imageVector = if (expanded) Icons.Rounded.KeyboardArrowUp else Icons.Rounded.KeyboardArrowDown,
                        contentDescription = null,
                        tint = MaterialTheme.colorScheme.primary,
                        modifier = Modifier.size(24.dp)
                    )
                }
            }

            AnimatedVisibility(visible = expanded) {
                Column(verticalArrangement = Arrangement.spacedBy(12.dp)) {
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.End
                    ) {
                        OutlinedButton(
                            onClick = onRetry,
                            enabled = !loading
                        ) {
                            Icon(Icons.Rounded.Refresh, contentDescription = null, modifier = Modifier.size(17.dp))
                        }
                    }

                    when {
                        loading && releases.isEmpty() -> {
                            UpdateInfoRow(
                                icon = Icons.Rounded.Refresh,
                                title = stringResource(R.string.settings_updates_history_loading),
                                body = stringResource(R.string.settings_updates_source_body)
                            )
                        }

                        errorMessage != null && releases.isEmpty() -> {
                            UpdateInfoRow(
                                icon = Icons.Rounded.ErrorOutline,
                                title = stringResource(R.string.settings_updates_history_error),
                                body = errorMessage
                            )
                        }

                        releases.isEmpty() -> {
                            Text(
                                text = stringResource(R.string.settings_updates_history_empty),
                                style = MaterialTheme.typography.bodyMedium,
                                color = MaterialTheme.colorScheme.onSurfaceVariant
                            )
                        }

                        else -> {
                            releases.forEach { release ->
                                ReleaseHistoryRow(
                                    release = release,
                                    onClick = { onReleaseClick(release) }
                                )
                            }
                            if (errorMessage != null) {
                                Text(
                                    text = errorMessage,
                                    style = MaterialTheme.typography.bodySmall,
                                    color = MaterialTheme.colorScheme.error
                                )
                            }
                        }
                    }
                }
            }
        }
    }
}

@Composable
private fun ReleaseHistoryRow(
    release: AppUpdateRelease,
    onClick: () -> Unit
) {
    Surface(
        modifier = Modifier.fillMaxWidth(),
        shape = RoundedCornerShape(18.dp),
        color = MaterialTheme.colorScheme.surfaceContainerHigh,
        tonalElevation = 1.dp,
        shadowElevation = 2.dp,
        border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.55f)),
        onClick = onClick
    ) {
        Row(
            modifier = Modifier.padding(12.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            Box(
                modifier = Modifier
                    .size(40.dp)
                    .background(
                        color = MaterialTheme.colorScheme.primary.copy(alpha = 0.10f),
                        shape = RoundedCornerShape(14.dp)
                    ),
                contentAlignment = Alignment.Center
            ) {
                Icon(
                    imageVector = Icons.Rounded.NewReleases,
                    contentDescription = null,
                    tint = MaterialTheme.colorScheme.primary,
                    modifier = Modifier.size(21.dp)
                )
            }
            Column(modifier = Modifier.weight(1f), verticalArrangement = Arrangement.spacedBy(3.dp)) {
                Text(
                    text = release.displayName,
                    style = MaterialTheme.typography.bodyLarge.copy(fontWeight = FontWeight.SemiBold),
                    color = MaterialTheme.colorScheme.onSurface,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis
                )
                Text(
                    text = releaseHistorySummary(release),
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis
                )
            }
            Icon(
                imageVector = Icons.AutoMirrored.Rounded.OpenInNew,
                contentDescription = null,
                tint = MaterialTheme.colorScheme.primary,
                modifier = Modifier.size(18.dp)
            )
        }
    }
}

@Composable
private fun ReleaseHistoryDialog(
    release: AppUpdateRelease,
    parallelDownloadProgress: Float?,
    parallelDownloaded: Boolean,
    onDismiss: () -> Unit,
    onOpenRelease: () -> Unit,
    onDownloadParallelRelease: () -> Unit,
    onInstallDownloadedParallelRelease: () -> Unit
) {
    val progressValue by animateFloatAsState(targetValue = parallelDownloadProgress ?: 0f, label = "parallel-apk-progress")
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
                shape = RoundedCornerShape(28.dp),
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
                                    shape = RoundedCornerShape(16.dp)
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

                    SectionCard(
                        title = stringResource(R.string.settings_updates_release_notes_title),
                        contentPadding = androidx.compose.foundation.layout.PaddingValues(14.dp)
                    ) {
                        Text(
                            text = displayReleaseNotes(release.body)
                                .ifBlank { stringResource(R.string.settings_updates_release_notes_empty) },
                            style = MaterialTheme.typography.bodyMedium,
                            color = MaterialTheme.colorScheme.onSurfaceVariant
                        )
                    }

                    SectionCard(
                        title = stringResource(R.string.settings_updates_parallel_title),
                        contentPadding = androidx.compose.foundation.layout.PaddingValues(14.dp)
                    ) {
                        Column(verticalArrangement = Arrangement.spacedBy(12.dp)) {
                            UpdateInfoRow(
                                icon = Icons.Rounded.InstallMobile,
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

                            AnimatedVisibility(visible = parallelDownloadProgress != null) {
                                Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                                    LinearProgressIndicator(
                                        progress = { progressValue.coerceIn(0f, 1f) },
                                        modifier = Modifier.fillMaxWidth()
                                    )
                                    Text(
                                        text = stringResource(
                                            R.string.settings_updates_parallel_downloading,
                                            (progressValue * 100).toInt().coerceIn(0, 100)
                                        ),
                                        style = MaterialTheme.typography.bodySmall,
                                        color = MaterialTheme.colorScheme.primary
                                    )
                                }
                            }

                            Button(
                                onClick = if (parallelDownloaded) onInstallDownloadedParallelRelease else onDownloadParallelRelease,
                                enabled = release.hasParallelApk && parallelDownloadProgress == null,
                                modifier = Modifier.fillMaxWidth()
                            ) {
                                Icon(
                                    imageVector = if (parallelDownloaded) Icons.Rounded.InstallMobile else Icons.Rounded.CloudDownload,
                                    contentDescription = null,
                                    modifier = Modifier.size(18.dp)
                                )
                                Text(
                                    text = stringResource(
                                        if (parallelDownloaded) R.string.settings_updates_parallel_install_ready
                                        else R.string.settings_updates_parallel_download
                                    ),
                                    modifier = Modifier.padding(start = 8.dp)
                                )
                            }
                        }
                    }

                    Button(
                        onClick = onOpenRelease,
                        modifier = Modifier.fillMaxWidth()
                    ) {
                        Icon(Icons.AutoMirrored.Rounded.OpenInNew, contentDescription = null, modifier = Modifier.size(18.dp))
                        Text(
                            text = stringResource(R.string.settings_updates_open_release),
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

private fun AppUpdateRelease.updateKey(): String {
    return tagName.ifBlank { htmlUrl.ifBlank { displayName } }
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
private fun CleanInstallDialog(
    release: AppUpdateRelease?,
    downloaded: Boolean,
    onDismiss: () -> Unit,
    onConfirm: () -> Unit
) {
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
                    .widthIn(max = 560.dp)
                    .heightIn(max = maxHeight),
                shape = RoundedCornerShape(28.dp),
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
                                    shape = RoundedCornerShape(16.dp)
                                ),
                            contentAlignment = Alignment.Center
                        ) {
                            Icon(
                                imageVector = Icons.Rounded.InstallMobile,
                                contentDescription = null,
                                tint = MaterialTheme.colorScheme.primary,
                                modifier = Modifier.size(26.dp)
                            )
                        }
                        Text(
                            text = stringResource(R.string.settings_updates_clean_dialog_title),
                            style = MaterialTheme.typography.titleLarge.copy(fontWeight = FontWeight.Bold),
                            color = MaterialTheme.colorScheme.onSurface,
                            modifier = Modifier.weight(1f)
                        )
                    }

                    HorizontalDivider(color = MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.5f))

                    Text(
                        text = stringResource(R.string.settings_updates_clean_dialog_body),
                        style = MaterialTheme.typography.bodyLarge,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )

                    release?.let {
                        Surface(
                            modifier = Modifier.fillMaxWidth(),
                            shape = RoundedCornerShape(18.dp),
                            color = MaterialTheme.colorScheme.surfaceContainerHigh
                        ) {
                            Row(
                                modifier = Modifier.padding(12.dp),
                                verticalAlignment = Alignment.CenterVertically,
                                horizontalArrangement = Arrangement.spacedBy(10.dp)
                            ) {
                                Icon(
                                    imageVector = Icons.Rounded.CloudDownload,
                                    contentDescription = null,
                                    tint = MaterialTheme.colorScheme.primary,
                                    modifier = Modifier.size(22.dp)
                                )
                                Column(modifier = Modifier.weight(1f)) {
                                    Text(
                                        text = it.displayName,
                                        style = MaterialTheme.typography.bodyLarge.copy(fontWeight = FontWeight.SemiBold),
                                        color = MaterialTheme.colorScheme.onSurface
                                    )
                                    Text(
                                        text = releaseSummary(it),
                                        style = MaterialTheme.typography.bodyMedium,
                                        color = MaterialTheme.colorScheme.onSurfaceVariant
                                    )
                                }
                            }
                        }
                    }

                    Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                        Button(
                            onClick = onConfirm,
                            modifier = Modifier.fillMaxWidth()
                        ) {
                            Icon(
                                imageVector = if (downloaded) Icons.Rounded.InstallMobile else Icons.Rounded.CloudDownload,
                                contentDescription = null,
                                modifier = Modifier.size(18.dp)
                            )
                            Text(
                                text = stringResource(
                                    if (downloaded) R.string.settings_updates_install_anyway
                                    else R.string.settings_updates_download_anyway
                                ),
                                modifier = Modifier.padding(start = 8.dp)
                            )
                        }
                        TextButton(
                            onClick = onDismiss,
                            modifier = Modifier.fillMaxWidth()
                        ) {
                            Text(stringResource(R.string.settings_updates_cancel))
                        }
                    }
                }
            }
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
    return String.format(java.util.Locale.US, "%.1f %s", value, units[unitIndex])
}

private fun releaseSummary(release: AppUpdateRelease): String {
    return listOfNotNull(
        release.tagName.takeIf { it.isNotBlank() },
        formatBytes(release.apkSizeBytes).takeIf { it != "-" }
    ).joinToString(" • ")
}

private fun releaseHistorySummary(release: AppUpdateRelease): String {
    return listOfNotNull(
        release.tagName.takeIf { it.isNotBlank() },
        formatReleaseDate(release.publishedAt).takeIf { it != "-" },
        formatBytes(release.apkSizeBytes).takeIf { it != "-" }
    ).joinToString(" • ")
}

private fun formatReleaseDate(value: String): String {
    if (value.isBlank()) return "-"
    return runCatching {
        Instant.parse(value)
            .atZone(ZoneId.systemDefault())
            .format(DateTimeFormatter.ofPattern("MMM d, yyyy", Locale.US))
    }.getOrDefault(value.substringBefore('T').ifBlank { "-" })
}

private fun displayReleaseNotes(body: String): String {
    return body
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
}

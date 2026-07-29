package com.sbro.emucorev.ui.settings

import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.rounded.OpenInNew
import androidx.compose.material.icons.rounded.ErrorOutline
import androidx.compose.material.icons.rounded.NewReleases
import androidx.compose.material.icons.rounded.Refresh
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.CircularProgressIndicator
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
import androidx.compose.ui.platform.LocalUriHandler
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
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
        shape = RoundedCornerShape(24.dp),
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
                        shape = RoundedCornerShape(18.dp),
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
                                listOfNotNull(
                                    release.tagName.takeIf(String::isNotBlank),
                                    formatReleaseDate(release.publishedAt).takeIf { it != "-" }
                                ).joinToString(" • "),
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
    AlertDialog(
        onDismissRequest = onDismiss,
        icon = { Icon(Icons.Rounded.NewReleases, contentDescription = null) },
        title = { Text(release.displayName) },
        text = {
            Column(verticalArrangement = Arrangement.spacedBy(10.dp)) {
                Text(
                    "${stringResource(R.string.settings_updates_version_label)}: ${release.tagName}"
                )
                Text(
                    "${stringResource(R.string.settings_updates_published_label)}: ${formatReleaseDate(release.publishedAt)}"
                )
                Text(
                    displayReleaseNotes(release.body)
                        .ifBlank { stringResource(R.string.settings_updates_release_notes_empty) },
                    style = MaterialTheme.typography.bodyMedium,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
            }
        },
        confirmButton = {
            Button(
                onClick = {
                    release.htmlUrl.takeIf(String::isNotBlank)?.let(uriHandler::openUri)
                    onDismiss()
                }
            ) {
                Icon(Icons.AutoMirrored.Rounded.OpenInNew, contentDescription = null, modifier = Modifier.size(18.dp))
                Text(stringResource(R.string.settings_updates_open_release), modifier = Modifier.padding(start = 8.dp))
            }
        },
        dismissButton = {
            TextButton(onClick = onDismiss) { Text(stringResource(R.string.common_close)) }
        }
    )
}

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

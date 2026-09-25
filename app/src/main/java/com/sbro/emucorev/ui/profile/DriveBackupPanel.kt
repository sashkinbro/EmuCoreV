package com.sbro.emucorev.ui.profile

import android.app.Activity
import android.content.Context
import android.content.ContextWrapper
import android.text.format.Formatter
import android.text.format.DateUtils
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.IntentSenderRequest
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.verticalScroll
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.OpenInNew
import androidx.compose.material.icons.filled.CloudDone
import androidx.compose.material.icons.filled.CloudUpload
import androidx.compose.material.icons.filled.CloudQueue
import androidx.compose.material.icons.filled.DeleteOutline
import androidx.compose.material.icons.filled.Restore
import androidx.compose.material.icons.filled.AccountCircle
import androidx.compose.material.icons.filled.CheckCircle
import androidx.compose.material.icons.filled.Close
import androidx.compose.material.icons.filled.Info
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.Alignment
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalUriHandler
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.semantics.LiveRegionMode
import androidx.compose.ui.semantics.liveRegion
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.unit.dp
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.lifecycle.ViewModelStoreOwner
import androidx.lifecycle.viewmodel.compose.LocalViewModelStoreOwner
import com.sbro.emucorev.R
import com.sbro.emucorev.data.ProfileDeviceInfoProvider
import com.sbro.emucorev.data.drive.DriveBackupArchive
import com.sbro.emucorev.data.drive.DriveBackupEntry
import com.sbro.emucorev.data.drive.DriveOperation
import com.sbro.emucorev.ui.common.UrlImage
import java.text.DateFormat
import java.text.NumberFormat
import java.util.Date
import com.sbro.emucorev.ui.theme.neon.neonShape
import com.sbro.emucorev.ui.theme.neon.neonChipShape
import com.sbro.emucorev.ui.theme.neon.neonButtonShape

@Composable
internal fun DriveBackupPanel(viewModel: DriveBackupViewModel = sharedDriveViewModel()) {
    val state by viewModel.state.collectAsState()
    val context = LocalContext.current
    val uriHandler = LocalUriHandler.current
    val settings = state.settings
    var restore by remember { mutableStateOf<DriveBackupEntry?>(null) }
    var delete by remember { mutableStateOf<DriveBackupEntry?>(null) }
    var disconnect by remember { mutableStateOf(false) }
    val authorization = rememberLauncherForActivityResult(ActivityResultContracts.StartIntentSenderForResult()) {
        viewModel.authorizationResult(it.resultCode, it.data)
    }
    LaunchedEffect(state.authorization) {
        state.authorization?.let {
            viewModel.authorizationLaunched()
            authorization.launch(IntentSenderRequest.Builder(it).build())
        }
    }
    Column(verticalArrangement = Arrangement.spacedBy(14.dp)) {
        Surface(modifier = Modifier.fillMaxWidth(), shape = neonShape(20.dp),
            color = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.5f), contentColor = MaterialTheme.colorScheme.onSurface,
            border = profileCardBorder()) {
            Row(Modifier.fillMaxWidth().padding(18.dp), horizontalArrangement = Arrangement.spacedBy(14.dp)) {
                Icon(Icons.Default.CloudUpload, null, tint = MaterialTheme.colorScheme.primary, modifier = Modifier.size(28.dp))
                Column(Modifier.weight(1f), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                    Text(stringResource(R.string.drive_title), style = MaterialTheme.typography.titleMedium, fontWeight = FontWeight.SemiBold)
                    Text(stringResource(R.string.drive_description), style = MaterialTheme.typography.bodyMedium, color = MaterialTheme.colorScheme.onSurfaceVariant)
                }
            }
        }
        if (!settings.connected || settings.needsAuthorization) {
            Button(
                shape = neonButtonShape(),onClick = { context.activity()?.let(viewModel::connect) }, enabled = !state.busy, modifier = Modifier.fillMaxWidth()) {
                Text(stringResource(if (settings.needsAuthorization) R.string.drive_reconnect else R.string.drive_connect))
            }
        }
        if (settings.connected) {
            DriveSection {
                Row(
                    modifier = Modifier.fillMaxWidth().padding(horizontal = 16.dp),
                    verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(12.dp)
                ) {
                    Surface(shape = CircleShape, color = MaterialTheme.colorScheme.primary.copy(alpha = 0.12f),
                        modifier = Modifier.size(56.dp), border = profileCardBorder()) {
                        Box(contentAlignment = Alignment.Center) {
                            Icon(Icons.Default.AccountCircle, null, tint = MaterialTheme.colorScheme.primary, modifier = Modifier.size(32.dp))
                            if (settings.photoUrl.isNotBlank()) UrlImage(
                                imageUrl = settings.photoUrl.takeIf { it.isNotBlank() },
                                contentDescription = settings.displayName.ifBlank { settings.email },
                                fallbackLabel = settings.displayName,
                                modifier = Modifier.fillMaxSize())
                        }
                    }
                    Column(Modifier.weight(1f), verticalArrangement = Arrangement.spacedBy(4.dp)) {
                        Text(settings.displayName.ifBlank { settings.email }, style = MaterialTheme.typography.titleSmall,
                            fontWeight = FontWeight.SemiBold, maxLines = 2, overflow = TextOverflow.Ellipsis)
                        if (settings.displayName.isNotBlank()) Text(settings.email, style = MaterialTheme.typography.bodySmall,
                            color = MaterialTheme.colorScheme.onSurfaceVariant, maxLines = 2, overflow = TextOverflow.Ellipsis)
                        Text(stringResource(R.string.drive_private), style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
                    }
                    if (!settings.needsAuthorization) Icon(Icons.Default.CheckCircle, null, tint = MaterialTheme.colorScheme.primary, modifier = Modifier.size(20.dp))
                }
                BoxWithConstraints(modifier = Modifier.fillMaxWidth().padding(horizontal = 16.dp)) {
                    val isNarrow = maxWidth < 360.dp
                    if (isNarrow) {
                        Column(modifier = Modifier.fillMaxWidth(), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                            TextButton(onClick = { disconnect = true }, enabled = !state.busy, modifier = Modifier.fillMaxWidth()) {
                                Text(stringResource(R.string.drive_disconnect))
                            }
                            OutlinedButton(
                                shape = neonButtonShape(),onClick = { context.activity()?.let(viewModel::connect) }, enabled = !state.busy, modifier = Modifier.fillMaxWidth()) {
                                Text(stringResource(R.string.drive_reconnect), textAlign = TextAlign.Center)
                            }
                        }
                    } else {
                        Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(8.dp), verticalAlignment = Alignment.CenterVertically) {
                            OutlinedButton(
                                shape = neonButtonShape(),onClick = { context.activity()?.let(viewModel::connect) }, enabled = !state.busy, modifier = Modifier.weight(1f)) {
                                Text(stringResource(R.string.drive_reconnect))
                            }
                            TextButton(onClick = { disconnect = true }, enabled = !state.busy) { Text(stringResource(R.string.drive_disconnect)) }
                        }
                    }
                }
            }
            if (settings.lastBackupMs > 0) {
                Surface(shape = neonShape(18.dp), border = profileCardBorder()) {
                ListItem(headlineContent = { Text(stringResource(R.string.drive_last_backup)) },
                    supportingContent = { Text("${date(settings.lastBackupMs)} · ${Formatter.formatFileSize(context, settings.lastSize)}") },
                    leadingContent = { Icon(Icons.Default.CloudDone, null) })
                }
            }
            if (state.queued || state.operation.phase.isNotBlank()) {
                DriveTransferProgress(state.operation, state.queued, viewModel::cancel)
            }
            Button(
                shape = neonButtonShape(),onClick = viewModel::backup, enabled = !state.busy && !settings.needsAuthorization, modifier = Modifier.fillMaxWidth()) {
                Icon(Icons.Default.CloudUpload, null); Spacer(Modifier.width(8.dp)); Text(stringResource(R.string.drive_backup_now))
            }
            DriveToggle(R.string.drive_include_save_data, settings.includeSaveData, !state.busy) { value -> viewModel.configure { it.copy(includeSaveData = value) } }
            HorizontalDivider()
            DriveSection {
                Text(stringResource(R.string.drive_schedule), modifier = Modifier.padding(horizontal = 16.dp), style = MaterialTheme.typography.titleMedium)
                Row(
                    modifier = Modifier.horizontalScroll(rememberScrollState()).padding(horizontal = 16.dp),
                    horizontalArrangement = Arrangement.spacedBy(8.dp)
                ) {
                    listOf(0, 6, 12, 24).forEach { hours ->
                        FilterChip(
                            shape = neonChipShape(),selected = settings.intervalHours == hours, enabled = !state.busy,
                            onClick = { viewModel.configure { it.copy(intervalHours = hours) } },
                            label = { Text(if (hours == 0) stringResource(R.string.drive_manual) else stringResource(R.string.drive_hours, hours)) })
                    }
                }
            }
            DriveToggle(R.string.drive_after_game, settings.afterGame, !state.busy) { value -> viewModel.configure { it.copy(afterGame = value) } }
            DriveToggle(R.string.drive_wifi, settings.wifiOnly, !state.busy) { value -> viewModel.configure { it.copy(wifiOnly = value) } }
            DriveToggle(R.string.drive_charging, settings.chargingOnly, !state.busy) { value -> viewModel.configure { it.copy(chargingOnly = value) } }
            Text(stringResource(R.string.drive_schedule_note), style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
            DriveSection {
                Text(stringResource(R.string.drive_retention, settings.keepCopies), modifier = Modifier.padding(horizontal = 16.dp), style = MaterialTheme.typography.titleSmall)
                Row(
                    modifier = Modifier.horizontalScroll(rememberScrollState()).padding(horizontal = 16.dp),
                    horizontalArrangement = Arrangement.spacedBy(8.dp)
                ) {
                    listOf(1, 3, 5, 10, 20).forEach { count ->
                        FilterChip(
                            shape = neonChipShape(),selected = settings.keepCopies == count, enabled = !state.busy,
                            onClick = { viewModel.configure { it.copy(keepCopies = count) } }, label = { Text(count.toString()) })
                    }
                }
            }
            HorizontalDivider()
            Row(verticalAlignment = Alignment.CenterVertically) {
                Text(stringResource(R.string.drive_history), Modifier.weight(1f), style = MaterialTheme.typography.titleMedium)
                TextButton(onClick = viewModel::refresh, enabled = !state.busy) {
                    if (state.loading) {
                        CircularProgressIndicator(modifier = Modifier.size(16.dp), strokeWidth = 2.dp)
                        Spacer(Modifier.width(8.dp))
                    }
                    Text(stringResource(R.string.drive_refresh))
                }
            }
            if (state.copies.isEmpty()) DriveSection {
                Column(Modifier.fillMaxWidth().padding(horizontal = 16.dp, vertical = 12.dp), horizontalAlignment = Alignment.CenterHorizontally,
                    verticalArrangement = Arrangement.spacedBy(12.dp)) {
                    Surface(shape = CircleShape, color = MaterialTheme.colorScheme.primary.copy(alpha = 0.1f), modifier = Modifier.size(64.dp)) {
                        Box(contentAlignment = Alignment.Center) {
                            Icon(Icons.Default.CloudQueue, null, tint = MaterialTheme.colorScheme.primary, modifier = Modifier.size(30.dp))
                        }
                    }
                    Text(stringResource(R.string.drive_empty), style = MaterialTheme.typography.titleSmall,
                        fontWeight = FontWeight.SemiBold, textAlign = TextAlign.Center)
                    Text(stringResource(R.string.drive_empty_description), style = MaterialTheme.typography.bodyMedium,
                        color = MaterialTheme.colorScheme.onSurfaceVariant, textAlign = TextAlign.Center)
                    OutlinedButton(
                        shape = neonButtonShape(),onClick = viewModel::backup, enabled = !state.busy && !settings.needsAuthorization) {
                        Icon(Icons.Default.CloudUpload, null, modifier = Modifier.size(18.dp))
                        Spacer(Modifier.width(8.dp))
                        Text(stringResource(R.string.drive_backup_now))
                    }
                }
            }
            state.copies.forEach { copy ->
                Surface(modifier = Modifier.fillMaxWidth(), shape = neonShape(18.dp), color = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.5f),
                    border = profileCardBorder()) {
                    Column(Modifier.padding(14.dp), verticalArrangement = Arrangement.spacedBy(5.dp)) {
                        Text(copy.device, fontWeight = FontWeight.SemiBold)
                        Text(date(copy.createdAt), style = MaterialTheme.typography.bodyMedium)
                        Text("${Formatter.formatFileSize(context, copy.size)} · ${copy.coreVersion}", style = MaterialTheme.typography.bodySmall)
                        Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.End) {
                            TextButton(onClick = { restore = copy }, enabled = !state.busy) {
                                Icon(Icons.Default.Restore, null); Spacer(Modifier.width(6.dp)); Text(stringResource(R.string.drive_restore))
                            }
                            IconButton(onClick = { delete = copy }, enabled = !state.busy) { Icon(Icons.Default.DeleteOutline, stringResource(R.string.drive_delete)) }
                        }
                    }
                }
            }
            TextButton(onClick = { uriHandler.openUri("https://drive.google.com/drive/my-drive") }) {
                Text(stringResource(R.string.drive_open)); Spacer(Modifier.width(6.dp)); Icon(Icons.AutoMirrored.Filled.OpenInNew, null)
            }
        }
        val message = state.message.ifBlank { settings.lastError }
        if (message.isNotBlank()) {
            val success = message == "restored"
            val accent = if (success) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.error
            Surface(modifier = Modifier.fillMaxWidth().semantics { liveRegion = LiveRegionMode.Polite },
                shape = neonShape(18.dp), color = accent.copy(alpha = 0.08f), border = profileCardBorder()) {
                Row(Modifier.padding(start = 16.dp, top = 10.dp, bottom = 10.dp, end = 4.dp),
                    verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(12.dp)) {
                    Icon(if (success) Icons.Default.CheckCircle else Icons.Default.Info, null, tint = accent)
                    Text(stringResource(errorString(message)), modifier = Modifier.weight(1f),
                        style = MaterialTheme.typography.bodyMedium, color = MaterialTheme.colorScheme.onSurface)
                    IconButton(onClick = viewModel::dismissMessage) { Icon(Icons.Default.Close, stringResource(R.string.common_close)) }
                }
            }
        }
    }
    restore?.let { copy ->
        var categories by remember(copy.id) { mutableStateOf(DriveBackupArchive.ALL_CATEGORIES) }
        AlertDialog(onDismissRequest = { restore = null }, title = { Text(stringResource(R.string.drive_restore)) },
            text = {
                Column(Modifier.verticalScroll(rememberScrollState())) {
                    Text(stringResource(R.string.drive_restore_warning))
                    if (copy.coreVersion != ProfileDeviceInfoProvider.CORE_VERSION) Text(stringResource(R.string.drive_core_warning), color = MaterialTheme.colorScheme.error)
                    listOf("settings" to R.string.drive_category_settings, "customization" to R.string.drive_category_style,
                        "save-data" to R.string.drive_category_save_data, "trophies" to R.string.drive_category_states,
                        "per-game" to R.string.drive_category_patches, "cheats" to R.string.drive_category_cheats).forEach { (key, label) ->
                        Row(verticalAlignment = Alignment.CenterVertically) {
                            Checkbox(key in categories, onCheckedChange = { categories = if (it) categories + key else categories - key })
                            Text(stringResource(label))
                        }
                    }
                }
            }, confirmButton = { TextButton(enabled = categories.isNotEmpty(), onClick = { viewModel.restore(copy, categories); restore = null }) { Text(stringResource(R.string.drive_restore)) } },
            dismissButton = { TextButton(onClick = { restore = null }) { Text(stringResource(R.string.drive_cancel)) } })
    }
    if (disconnect || delete != null) AlertDialog(onDismissRequest = { disconnect = false; delete = null },
        title = { Text(stringResource(if (disconnect) R.string.drive_disconnect else R.string.drive_delete)) },
        text = { Text(stringResource(if (disconnect) R.string.drive_disconnect_warning else R.string.drive_delete_warning)) },
        confirmButton = { TextButton(onClick = {
            if (disconnect) viewModel.disconnect() else delete?.let(viewModel::delete)
            disconnect = false; delete = null
        }) { Text(stringResource(if (disconnect) R.string.drive_disconnect else R.string.drive_delete)) } },
        dismissButton = { TextButton(onClick = { disconnect = false; delete = null }) { Text(stringResource(R.string.drive_cancel)) } })
}

@Composable
private fun DriveTransferProgress(operation: DriveOperation, queued: Boolean, cancel: () -> Unit) {
    val context = LocalContext.current
    DriveSection {
        Row(
            modifier = Modifier.fillMaxWidth().padding(horizontal = 16.dp),
            verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            Text(stringResource(when (operation.phase) {
                "prepare" -> R.string.drive_preparing
                "upload" -> R.string.drive_uploading
                "download" -> R.string.drive_downloading
                "verify" -> R.string.drive_verifying
                "restore" -> R.string.drive_restoring
                else -> if (queued) R.string.drive_waiting else R.string.drive_working
            }), modifier = Modifier.weight(1f), style = MaterialTheme.typography.titleSmall, fontWeight = FontWeight.SemiBold)
            if (operation.percent >= 0) Text(NumberFormat.getPercentInstance().format(operation.percent / 100.0),
                style = MaterialTheme.typography.titleMedium.copy(fontFeatureSettings = "tnum"),
                fontWeight = FontWeight.SemiBold, color = MaterialTheme.colorScheme.primary)
        }
        if (operation.percent >= 0) LinearProgressIndicator(progress = { operation.percent / 100f }, modifier = Modifier.fillMaxWidth().padding(horizontal = 16.dp))
        else LinearProgressIndicator(modifier = Modifier.fillMaxWidth().padding(horizontal = 16.dp))
        if (operation.totalBytes > 0) {
            Text(
                stringResource(R.string.drive_transfer_amount, Formatter.formatFileSize(context, operation.transferredBytes),
                Formatter.formatFileSize(context, operation.totalBytes)),
                modifier = Modifier.padding(horizontal = 16.dp),
                style = MaterialTheme.typography.bodyMedium.copy(fontFeatureSettings = "tnum")
            )
            Row(modifier = Modifier.fillMaxWidth().padding(horizontal = 16.dp), horizontalArrangement = Arrangement.spacedBy(12.dp)) {
                Text(if (operation.bytesPerSecond > 0) stringResource(R.string.drive_transfer_speed,
                    Formatter.formatFileSize(context, operation.bytesPerSecond)) else "—",
                    modifier = Modifier.weight(1f), style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant)
                Text(if (operation.remainingSeconds > 0) stringResource(R.string.drive_transfer_remaining,
                    DateUtils.formatElapsedTime(operation.remainingSeconds)) else "—",
                    modifier = Modifier.weight(1f), textAlign = TextAlign.End, style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant)
            }
        }
        TextButton(onClick = cancel, modifier = Modifier.align(Alignment.End).padding(end = 16.dp)) { Text(stringResource(R.string.drive_cancel)) }
    }
}

@Composable
private fun DriveToggle(label: Int, checked: Boolean, enabled: Boolean, onChange: (Boolean) -> Unit) {
    Surface(modifier = Modifier.fillMaxWidth(), shape = neonShape(16.dp),
        color = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.4f), border = profileCardBorder()) {
        Row(Modifier.fillMaxWidth().padding(horizontal = 16.dp, vertical = 8.dp), verticalAlignment = Alignment.CenterVertically) {
            Text(stringResource(label), Modifier.weight(1f).padding(end = 12.dp), style = MaterialTheme.typography.bodyMedium)
            Switch(checked, onChange, enabled = enabled)
        }
    }
}

@Composable
private fun DriveSection(content: @Composable ColumnScope.() -> Unit) {
    Surface(modifier = Modifier.fillMaxWidth(), shape = neonShape(20.dp),
        color = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.4f), contentColor = MaterialTheme.colorScheme.onSurface,
        border = profileCardBorder()) {
        Column(Modifier.fillMaxWidth().padding(vertical = 16.dp), verticalArrangement = Arrangement.spacedBy(12.dp), content = content)
    }
}

private fun date(timestamp: Long) = DateFormat.getDateTimeInstance(DateFormat.MEDIUM, DateFormat.SHORT).format(Date(timestamp))
@Composable
private fun sharedDriveViewModel(): DriveBackupViewModel {
    val owner = LocalContext.current.activity() as? ViewModelStoreOwner ?: checkNotNull(LocalViewModelStoreOwner.current)
    return viewModel(viewModelStoreOwner = owner, key = "drive-backup")
}
private tailrec fun Context.activity(): Activity? = when (this) { is Activity -> this; is ContextWrapper -> baseContext.activity(); else -> null }
private fun errorString(reason: String) = when (reason) {
    "auth", "permission" -> R.string.drive_error_auth
    "configuration" -> R.string.drive_error_configuration
    "quota" -> R.string.drive_error_quota
    "space", "storage" -> R.string.drive_error_space
    "busy" -> R.string.drive_error_busy
    "invalid" -> R.string.drive_error_invalid
    "missing" -> R.string.drive_error_missing
    "network" -> R.string.drive_error_network
    "restored" -> R.string.drive_restored
    else -> R.string.drive_error_failed
}

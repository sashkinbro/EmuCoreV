package com.sbro.emucorev.ui.saves

import android.net.Uri
import android.widget.Toast
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.expandVertically
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.shrinkVertically
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.asPaddingValues
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.navigationBarsPadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.statusBarsIgnoringVisibility
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.Close
import androidx.compose.material.icons.rounded.Delete
import androidx.compose.material.icons.rounded.FolderOpen
import androidx.compose.material.icons.rounded.Refresh
import androidx.compose.material.icons.rounded.Save
import androidx.compose.material.icons.rounded.Search
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.OutlinedTextFieldDefaults
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.lifecycle.viewmodel.compose.viewModel
import com.sbro.emucorev.R
import com.sbro.emucorev.data.SaveDataImportResult
import com.sbro.emucorev.data.VitaSaveDataEntry
import com.sbro.emucorev.ui.common.LocalImage
import com.sbro.emucorev.ui.common.NavigationBackButton
import com.sbro.emucorev.ui.common.PremiumLoadingAnimation
import com.sbro.emucorev.ui.common.rememberDebouncedClick
import com.sbro.emucorev.ui.theme.CardContentPadding
import com.sbro.emucorev.ui.theme.ScreenContentBottomPadding
import com.sbro.emucorev.ui.theme.ScreenHorizontalPadding
import com.sbro.emucorev.ui.theme.ScreenTopInsetOffset
import java.text.DateFormat
import java.util.Date
import java.util.Locale

@OptIn(ExperimentalLayoutApi::class)
@Composable
fun SaveDataScreen(
    focusTitleId: String?,
    onBackClick: () -> Unit,
    viewModel: SaveDataViewModel = viewModel()
) {
    val context = LocalContext.current
    val uiState by viewModel.uiState.collectAsState()
    val topInset = WindowInsets.statusBarsIgnoringVisibility.asPaddingValues().calculateTopPadding() + ScreenTopInsetOffset
    val backClick = rememberDebouncedClick(onClick = onBackClick)
    var searchExpanded by rememberSaveable { mutableStateOf(false) }
    var pendingExportSaveId by rememberSaveable { mutableStateOf<String?>(null) }
    var pendingImportSaveId by rememberSaveable { mutableStateOf<String?>(null) }
    var deleteSave by remember { mutableStateOf<VitaSaveDataEntry?>(null) }

    val exportSuccess = stringResource(R.string.save_manager_export_success)
    val exportFailed = stringResource(R.string.save_manager_export_failed)
    val deleteFailed = stringResource(R.string.save_manager_delete_failed)
    val importSuccessTemplate = stringResource(R.string.save_manager_import_success, "%s")
    val importFailed = stringResource(R.string.save_manager_import_failed)
    val importEmpty = stringResource(R.string.save_manager_import_empty)
    val importUnsafe = stringResource(R.string.save_manager_import_unsafe)
    val importUnknownTarget = stringResource(R.string.save_manager_import_unknown_target)

    val exportLauncher = rememberLauncherForActivityResult(ActivityResultContracts.CreateDocument("application/zip")) { uri: Uri? ->
        val saveId = pendingExportSaveId
        pendingExportSaveId = null
        if (uri != null && saveId != null) {
            viewModel.exportSave(saveId, uri) { result ->
                Toast.makeText(context, if (result.isSuccess) exportSuccess else exportFailed, Toast.LENGTH_SHORT).show()
            }
        }
    }
    val importLauncher = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri: Uri? ->
        val targetSaveId = pendingImportSaveId
        pendingImportSaveId = null
        if (uri != null) {
            viewModel.importSave(uri, targetSaveId) { result ->
                val message = when (result) {
                    is SaveDataImportResult.Success -> importSuccessTemplate.format(result.saveId)
                    SaveDataImportResult.EmptyArchive -> importEmpty
                    SaveDataImportResult.UnsafeArchive -> importUnsafe
                    SaveDataImportResult.UnknownTarget -> importUnknownTarget
                    is SaveDataImportResult.Failure -> result.error.message ?: importFailed
                }
                Toast.makeText(context, message, Toast.LENGTH_LONG).show()
            }
        }
    }

    LaunchedEffect(focusTitleId) {
        viewModel.refresh(focusTitleId)
    }

    LazyColumn(
        modifier = Modifier
            .fillMaxSize()
            .background(MaterialTheme.colorScheme.background)
            .navigationBarsPadding(),
        contentPadding = PaddingValues(
            start = ScreenHorizontalPadding,
            end = ScreenHorizontalPadding,
            top = topInset,
            bottom = ScreenContentBottomPadding
        ),
        verticalArrangement = Arrangement.spacedBy(14.dp)
    ) {
        item {
            SaveManagerHeader(
                searchExpanded = searchExpanded,
                onBackClick = backClick,
                onSearchClick = {
                    if (searchExpanded) {
                        viewModel.updateQuery("")
                    }
                    searchExpanded = !searchExpanded
                },
                onRefreshClick = { viewModel.refresh(focusTitleId) },
                onImportClick = {
                    pendingImportSaveId = uiState.focusTarget?.saveId
                    importLauncher.launch(arrayOf("application/zip", "application/octet-stream", "*/*"))
                }
            )
        }
        item {
            AnimatedVisibility(
                visible = searchExpanded,
                enter = fadeIn() + expandVertically(),
                exit = fadeOut() + shrinkVertically()
            ) {
                OutlinedTextField(
                    value = uiState.query,
                    onValueChange = viewModel::updateQuery,
                    modifier = Modifier.fillMaxWidth(),
                    placeholder = { Text(stringResource(R.string.save_manager_search_hint)) },
                    singleLine = true,
                    shape = RoundedCornerShape(20.dp),
                    colors = OutlinedTextFieldDefaults.colors(
                        focusedBorderColor = MaterialTheme.colorScheme.primary.copy(alpha = 0.5f),
                        unfocusedBorderColor = MaterialTheme.colorScheme.outline.copy(alpha = 0.55f),
                        focusedContainerColor = MaterialTheme.colorScheme.surface,
                        unfocusedContainerColor = MaterialTheme.colorScheme.surface
                    )
                )
            }
        }
        if (uiState.isLoading) {
            item {
                Box(
                    modifier = Modifier
                        .fillMaxWidth()
                        .heightIn(min = 360.dp),
                    contentAlignment = Alignment.Center
                ) {
                    PremiumLoadingAnimation(size = 64.dp)
                }
            }
        } else if (uiState.saves.isEmpty()) {
            item {
                EmptySaveState(
                    focusTitle = uiState.focusTarget?.title,
                    onImportClick = {
                        pendingImportSaveId = uiState.focusTarget?.saveId
                        importLauncher.launch(arrayOf("application/zip", "application/octet-stream", "*/*"))
                    }
                )
            }
        } else {
            items(uiState.saves, key = { it.saveId }) { save ->
                SaveDataCard(
                    save = save,
                    busy = uiState.busySaveId == save.saveId,
                    onExport = {
                        pendingExportSaveId = save.saveId
                        exportLauncher.launch("${save.saveId}-save.zip")
                    },
                    onImport = {
                        pendingImportSaveId = save.saveId
                        importLauncher.launch(arrayOf("application/zip", "application/octet-stream", "*/*"))
                    },
                    onDelete = { deleteSave = save }
                )
            }
        }
    }

    val targetDeleteSave = deleteSave
    if (targetDeleteSave != null) {
        AlertDialog(
            onDismissRequest = { deleteSave = null },
            title = { Text(stringResource(R.string.save_manager_delete_confirm_title)) },
            text = { Text(stringResource(R.string.save_manager_delete_confirm_body, targetDeleteSave.title)) },
            confirmButton = {
                TextButton(
                    onClick = {
                        deleteSave = null
                        viewModel.delete(targetDeleteSave.saveId) { deleted ->
                            if (!deleted) {
                                Toast.makeText(context, deleteFailed, Toast.LENGTH_SHORT).show()
                            }
                        }
                    }
                ) {
                    Text(stringResource(R.string.save_manager_delete))
                }
            },
            dismissButton = {
                TextButton(onClick = { deleteSave = null }) {
                    Text(stringResource(R.string.install_dialog_close))
                }
            }
        )
    }
}

@Composable
private fun SaveManagerHeader(
    searchExpanded: Boolean,
    onBackClick: () -> Unit,
    onSearchClick: () -> Unit,
    onRefreshClick: () -> Unit,
    onImportClick: () -> Unit
) {
    Row(
        modifier = Modifier.fillMaxWidth(),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.SpaceBetween
    ) {
        Row(
            modifier = Modifier.weight(1f),
            verticalAlignment = Alignment.CenterVertically
        ) {
            NavigationBackButton(onClick = onBackClick)
            Column(modifier = Modifier.padding(start = 14.dp)) {
                Text(
                    text = stringResource(R.string.save_manager_title),
                    style = MaterialTheme.typography.headlineSmall.copy(fontWeight = FontWeight.Bold),
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis
                )
            }
        }
        Row {
            IconButton(onClick = onSearchClick) {
                Icon(
                    imageVector = if (searchExpanded) Icons.Rounded.Close else Icons.Rounded.Search,
                    contentDescription = stringResource(R.string.save_manager_search_hint)
                )
            }
            IconButton(onClick = onRefreshClick) {
                Icon(Icons.Rounded.Refresh, contentDescription = stringResource(R.string.library_refresh))
            }
            IconButton(onClick = onImportClick) {
                Icon(Icons.Rounded.FolderOpen, contentDescription = stringResource(R.string.save_manager_import))
            }
        }
    }
}

@Composable
private fun SaveDataCard(
    save: VitaSaveDataEntry,
    busy: Boolean,
    onExport: () -> Unit,
    onImport: () -> Unit,
    onDelete: () -> Unit
) {
    Surface(
        modifier = Modifier.fillMaxWidth(),
        shape = RoundedCornerShape(24.dp),
        color = MaterialTheme.colorScheme.surface,
        tonalElevation = 3.dp,
        shadowElevation = 8.dp,
        border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.58f))
    ) {
        Column(
            modifier = Modifier.padding(CardContentPadding),
            verticalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            Row(
                horizontalArrangement = Arrangement.spacedBy(14.dp),
                verticalAlignment = Alignment.Top
            ) {
                Surface(
                    modifier = Modifier.size(64.dp),
                    shape = RoundedCornerShape(16.dp),
                    color = MaterialTheme.colorScheme.surfaceContainerHigh
                ) {
                    LocalImage(
                        path = save.iconPath,
                        contentDescription = save.title,
                        fallbackLabel = save.title,
                        modifier = Modifier.fillMaxSize()
                    )
                }
                Column(modifier = Modifier.weight(1f), verticalArrangement = Arrangement.spacedBy(4.dp)) {
                    Text(
                        text = save.title,
                        style = MaterialTheme.typography.titleMedium.copy(fontWeight = FontWeight.Bold),
                        maxLines = 2,
                        overflow = TextOverflow.Ellipsis
                    )
                    Text(
                        text = save.saveId,
                        style = MaterialTheme.typography.bodyMedium,
                        color = MaterialTheme.colorScheme.primary
                    )
                    Text(
                        text = "${formatSaveSize(save.sizeBytes)} • ${formatSaveDate(save.updatedAtMillis)}",
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                    if (!save.installed) {
                        Text(
                            text = stringResource(R.string.save_manager_orphan_save),
                            style = MaterialTheme.typography.labelMedium,
                            color = MaterialTheme.colorScheme.error
                        )
                    }
                }
            }
            if (busy) {
                LinearProgressIndicator(modifier = Modifier.fillMaxWidth())
            }
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.spacedBy(10.dp)
            ) {
                Button(
                    onClick = onExport,
                    enabled = !busy,
                    modifier = Modifier.weight(1f)
                ) {
                    Icon(Icons.Rounded.Save, contentDescription = null)
                    Text(stringResource(R.string.save_manager_export), modifier = Modifier.padding(start = 8.dp))
                }
                OutlinedButton(
                    onClick = onImport,
                    enabled = !busy,
                    modifier = Modifier.weight(1f)
                ) {
                    Icon(Icons.Rounded.FolderOpen, contentDescription = null)
                    Text(stringResource(R.string.save_manager_import), modifier = Modifier.padding(start = 8.dp))
                }
            }
            OutlinedButton(
                onClick = onDelete,
                enabled = !busy,
                modifier = Modifier.fillMaxWidth()
            ) {
                Icon(Icons.Rounded.Delete, contentDescription = null)
                Text(stringResource(R.string.save_manager_delete), modifier = Modifier.padding(start = 8.dp))
            }
        }
    }
}

@Composable
private fun EmptySaveState(
    focusTitle: String?,
    onImportClick: () -> Unit
) {
    Box(
        modifier = Modifier
            .fillMaxWidth()
            .heightIn(min = 360.dp),
        contentAlignment = Alignment.Center
    ) {
        Column(
            modifier = Modifier.widthIn(max = 520.dp),
            horizontalAlignment = Alignment.CenterHorizontally,
            verticalArrangement = Arrangement.spacedBy(16.dp)
        ) {
            Text(
                text = focusTitle?.let { stringResource(R.string.save_manager_empty_game_title, it) }
                    ?: stringResource(R.string.save_manager_empty_title),
                style = MaterialTheme.typography.titleLarge.copy(fontWeight = FontWeight.Bold),
                color = MaterialTheme.colorScheme.onBackground
            )
            Text(
                text = if (focusTitle != null) {
                    stringResource(R.string.save_manager_empty_game_body)
                } else {
                    stringResource(R.string.save_manager_empty_body)
                },
                style = MaterialTheme.typography.bodyMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
            Button(onClick = onImportClick) {
                Icon(Icons.Rounded.FolderOpen, contentDescription = null)
                Text(stringResource(R.string.save_manager_import), modifier = Modifier.padding(start = 8.dp))
            }
        }
    }
}

private fun formatSaveSize(bytes: Long): String {
    val kb = 1024.0
    val mb = kb * 1024.0
    val gb = mb * 1024.0
    return when {
        bytes >= gb -> String.format(Locale.US, "%.1f GB", bytes / gb)
        bytes >= mb -> String.format(Locale.US, "%.1f MB", bytes / mb)
        bytes >= kb -> String.format(Locale.US, "%.0f KB", bytes / kb)
        else -> "$bytes B"
    }
}

private fun formatSaveDate(millis: Long): String =
    DateFormat.getDateTimeInstance(DateFormat.MEDIUM, DateFormat.SHORT).format(Date(millis))

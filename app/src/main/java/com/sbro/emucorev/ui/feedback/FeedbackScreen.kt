@file:OptIn(androidx.compose.foundation.layout.ExperimentalLayoutApi::class)

package com.sbro.emucorev.ui.feedback

import com.sbro.emucorev.ui.common.horizontalBleed
import com.sbro.emucorev.ui.theme.neon.neonButtonShape
import com.sbro.emucorev.ui.theme.neon.neonChipShape
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.heightIn
import androidx.compose.material.icons.rounded.Check
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.ExposedDropdownMenuAnchorType
import androidx.compose.material3.ExposedDropdownMenuBox
import androidx.compose.material3.ExposedDropdownMenuDefaults
import androidx.compose.material3.OutlinedTextFieldDefaults
import android.net.Uri
import android.widget.Toast
import androidx.activity.compose.BackHandler
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.PickVisualMediaRequest
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.asPaddingValues
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.navigationBars
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.statusBarsIgnoringVisibility
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.rememberScrollState
import com.sbro.emucorev.ui.theme.neon.neonShape

import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.rounded.Send
import androidx.compose.material.icons.rounded.AttachFile
import androidx.compose.material.icons.rounded.Close
import androidx.compose.material.icons.rounded.Collections
import androidx.compose.material.icons.rounded.UploadFile
import com.sbro.emucorev.ui.common.AppAlertDialog as AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.FilterChip
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateListOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import com.sbro.emucorev.R
import com.sbro.emucorev.data.InstalledGameRepository
import com.sbro.emucorev.data.InstalledVitaGame
import com.sbro.emucorev.feedback.FeedbackAttachment
import com.sbro.emucorev.feedback.FeedbackAttachmentError
import com.sbro.emucorev.feedback.FeedbackAttachmentInspector
import com.sbro.emucorev.feedback.FeedbackLimits
import com.sbro.emucorev.feedback.FeedbackUploadScheduler
import com.sbro.emucorev.ui.common.ScreenTopBar
import java.util.Locale
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

private data class FeedbackCategory(val id: String, val label: String)

@Composable
@OptIn(ExperimentalMaterial3Api::class)
fun FeedbackScreen(onBackClick: () -> Unit) {
    val context = LocalContext.current
    val scope = rememberCoroutineScope()
    val attachments = remember { mutableStateListOf<FeedbackAttachment>() }
    var category by rememberSaveable { mutableStateOf("compatibility") }
    var selectedGameId by rememberSaveable { mutableStateOf<String?>(null) }
    var categoryMenuExpanded by remember { mutableStateOf(false) }
    var gameMenuExpanded by remember { mutableStateOf(false) }
    var isLoadingLibrary by remember { mutableStateOf(true) }
    var manualGame by rememberSaveable { mutableStateOf("") }
    var games by remember { mutableStateOf(emptyList<InstalledVitaGame>()) }
    var message by rememberSaveable { mutableStateOf("") }
    var isQueueing by remember { mutableStateOf(false) }
    var isInspectingAttachments by remember { mutableStateOf(false) }
    var showAttachmentSourceDialog by remember { mutableStateOf(false) }
    var showDiscardDialog by remember { mutableStateOf(false) }

    val categories = listOf(
        FeedbackCategory("compatibility", stringResource(R.string.feedback_category_compatibility)),
        FeedbackCategory("performance", stringResource(R.string.feedback_category_performance)),
        FeedbackCategory("graphics", stringResource(R.string.feedback_category_graphics)),
        FeedbackCategory("audio", stringResource(R.string.feedback_category_audio)),
        FeedbackCategory("controls", stringResource(R.string.feedback_category_controls)),
        FeedbackCategory("crash", stringResource(R.string.feedback_category_crash)),
        FeedbackCategory("feature", stringResource(R.string.feedback_category_feature)),
        FeedbackCategory("ui", stringResource(R.string.feedback_category_ui)),
        FeedbackCategory("other", stringResource(R.string.feedback_category_other))
    )
    val attachmentErrorTexts = mapOf(
        FeedbackAttachmentError.TooMany to stringResource(R.string.feedback_error_too_many_files),
        FeedbackAttachmentError.ItemTooLarge to stringResource(R.string.feedback_error_file_too_large),
        FeedbackAttachmentError.TotalTooLarge to stringResource(R.string.feedback_error_total_too_large),
        FeedbackAttachmentError.Unreadable to stringResource(R.string.feedback_error_file_unreadable)
    )
    val requiredMessage = stringResource(R.string.feedback_error_message_required)
    val queueFailedMessage = stringResource(R.string.feedback_error_queue_failed)
    val queuedMessage = stringResource(R.string.feedback_queued)

    LaunchedEffect(context) {
        games = withContext(Dispatchers.IO) {
            InstalledGameRepository().loadInstalledGames(context)
        }
        isLoadingLibrary = false
    }
    val selectedGame = remember(games, selectedGameId) {
        selectedGameId?.let { id -> games.firstOrNull { it.titleId == id } }
    }

    val filteredGames = remember(games, manualGame, selectedGameId, gameMenuExpanded) {
        val query = manualGame.trim()
        (if (!gameMenuExpanded || selectedGameId != null || query.isBlank()) games
        else games.filter { it.title.contains(query, ignoreCase = true) || it.titleId.contains(query, ignoreCase = true) }).take(100)
    }

    fun acceptUris(newUris: List<Uri>) {
        if (newUris.isEmpty()) return
        scope.launch {
            isInspectingAttachments = true
            try {
                val inspection = withContext(Dispatchers.IO) {
                    FeedbackAttachmentInspector.inspect(
                        context,
                        (attachments.map { it.uri } + newUris).distinct()
                    )
                }
                val error = inspection.error
                if (error != null) {
                    Toast.makeText(context, attachmentErrorTexts[error], Toast.LENGTH_LONG).show()
                } else {
                    attachments.clear()
                    attachments.addAll(inspection.attachments)
                }
            } finally {
                isInspectingAttachments = false
            }
        }
    }

    val mediaPicker = rememberLauncherForActivityResult(
        ActivityResultContracts.PickMultipleVisualMedia(FeedbackLimits.MAX_ATTACHMENTS)
    ) { uris -> acceptUris(uris) }
    val filePicker = rememberLauncherForActivityResult(
        ActivityResultContracts.OpenMultipleDocuments()
    ) { uris -> acceptUris(uris) }

    val hasChanges = message.isNotBlank() || manualGame.isNotBlank() || selectedGameId != null ||
        attachments.isNotEmpty() || category != "compatibility"
    val requestBack = {
        if (!isQueueing) {
            if (hasChanges) showDiscardDialog = true else onBackClick()
        }
    }
    BackHandler(onBack = requestBack)

    val submitFeedback: () -> Unit = {
        if (message.isBlank()) {
            Toast.makeText(context, requiredMessage, Toast.LENGTH_SHORT).show()
        } else if (!isQueueing && !isInspectingAttachments && FeedbackUploadScheduler.isConfigured) {
            scope.launch {
                isQueueing = true
                runCatching {
                    FeedbackUploadScheduler.enqueue(
                        context = context,
                        category = category,
                        gameTitle = selectedGame?.title ?: manualGame,
                        gameSerial = selectedGame?.titleId.orEmpty(),
                        message = message,
                        includeDiagnostics = true,
                        attachments = attachments.toList()
                    )
                }.onSuccess {
                    Toast.makeText(context, queuedMessage, Toast.LENGTH_LONG).show()
                    onBackClick()
                }.onFailure {
                    Toast.makeText(context, queueFailedMessage, Toast.LENGTH_LONG).show()
                }
                isQueueing = false
            }
        }
    }

    LazyColumn(
        modifier = Modifier
            .fillMaxSize()
            .background(MaterialTheme.colorScheme.background),
        contentPadding = PaddingValues(
            start = 8.dp,
            top = WindowInsets.statusBarsIgnoringVisibility.asPaddingValues().calculateTopPadding(),
            end = 8.dp,
            bottom = WindowInsets.navigationBars.asPaddingValues().calculateBottomPadding() + 18.dp
        ),
        verticalArrangement = Arrangement.spacedBy(8.dp),
        horizontalAlignment = Alignment.CenterHorizontally
    ) {
        item {
            ScreenTopBar(
                title = stringResource(R.string.feedback_title),
                subtitle = stringResource(R.string.feedback_subtitle),
                onBackClick = requestBack,
                modifier = Modifier
                    .fillMaxWidth()
                    .widthIn(max = 760.dp)
                    .padding(top = 12.dp, bottom = 2.dp),
                titleMaxLines = 2,
                subtitleMaxLines = 2
            )
        }
        if (!FeedbackUploadScheduler.isConfigured) {
            item { FeedbackConfigurationWarning(Modifier.widthIn(max = 760.dp)) }
        }
        item {
            Surface(
                modifier = Modifier
                    .fillMaxWidth()
                    .widthIn(max = 760.dp),
                shape = neonShape(20.dp),
                color = MaterialTheme.colorScheme.surface.copy(alpha = 0.22f),
                contentColor = MaterialTheme.colorScheme.onSurface,
                border = BorderStroke(
                    1.dp,
                    MaterialTheme.colorScheme.outline.copy(alpha = 0.16f)
                )
            ) {
                Column(
                    modifier = Modifier.padding(12.dp),
                    verticalArrangement = Arrangement.spacedBy(9.dp)
                ) {
                    Text(
                        text = stringResource(R.string.feedback_category_title),
                        style = MaterialTheme.typography.labelLarge,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                    ExposedDropdownMenuBox(
                        expanded = categoryMenuExpanded,
                        onExpandedChange = { categoryMenuExpanded = it }
                    ) {
                        OutlinedTextField(
                            value = categories.firstOrNull { it.id == category }?.label.orEmpty(),
                            onValueChange = {},
                            readOnly = true,
                            singleLine = true,
                            shape = neonShape(16.dp),
                            modifier = Modifier
                                .fillMaxWidth()
                                .menuAnchor(ExposedDropdownMenuAnchorType.PrimaryNotEditable),
                            trailingIcon = {
                                ExposedDropdownMenuDefaults.TrailingIcon(expanded = categoryMenuExpanded)
                            },
                            colors = feedbackTextFieldColors()
                        )
                        ExposedDropdownMenu(
                            expanded = categoryMenuExpanded,
                            onDismissRequest = { categoryMenuExpanded = false },
                            modifier = Modifier.heightIn(max = 384.dp),
                            shape = neonShape(18.dp),
                            containerColor = MaterialTheme.colorScheme.surface.copy(alpha = 0.98f),
                            border = BorderStroke(
                                1.dp,
                                MaterialTheme.colorScheme.outline.copy(alpha = 0.38f)
                            )
                        ) {
                            categories.forEach { item ->
                                val isSelected = item.id == category
                                DropdownMenuItem(
                                    text = {
                                        Text(
                                            item.label,
                                            color = if (isSelected) {
                                                MaterialTheme.colorScheme.primary
                                            } else {
                                                MaterialTheme.colorScheme.onSurface
                                            },
                                            fontWeight = if (isSelected) FontWeight.SemiBold else FontWeight.Medium
                                        )
                                    },
                                    trailingIcon = {
                                        if (isSelected) {
                                            Icon(
                                                Icons.Rounded.Check,
                                                contentDescription = null,
                                                tint = MaterialTheme.colorScheme.primary
                                            )
                                        }
                                    },
                                    onClick = {
                                        category = item.id
                                        categoryMenuExpanded = false
                                    }
                                )
                            }
                        }
                    }

                    ExposedDropdownMenuBox(
                        expanded = gameMenuExpanded,
                        onExpandedChange = {
                            if (!isLoadingLibrary && games.isNotEmpty()) gameMenuExpanded = it
                        }
                    ) {
                        OutlinedTextField(
                            value = selectedGame?.let { "${it.title} · ${it.titleId}" } ?: manualGame,
                            onValueChange = { value ->
                                selectedGameId = null
                                manualGame = value.take(FeedbackLimits.MAX_GAME_LENGTH)
                                gameMenuExpanded = games.isNotEmpty()
                            },
                            singleLine = true,
                            shape = neonShape(16.dp),
                            modifier = Modifier
                                .fillMaxWidth()
                                .menuAnchor(ExposedDropdownMenuAnchorType.PrimaryEditable),
                            label = { Text(stringResource(R.string.feedback_game_label)) },
                            placeholder = {
                                Text(
                                    stringResource(
                                        if (isLoadingLibrary) R.string.feedback_game_loading
                                        else R.string.feedback_game_placeholder
                                    ),
                                    maxLines = 1,
                                    overflow = TextOverflow.Ellipsis
                                )
                            },
                            trailingIcon = {
                                if (selectedGame != null) {
                                    IconButton(
                                        onClick = {
                                            selectedGameId = null
                                            manualGame = ""
                                        }
                                    ) {
                                        Icon(
                                            Icons.Rounded.Close,
                                            contentDescription = stringResource(R.string.feedback_game_clear_selection)
                                        )
                                    }
                                } else if (games.isNotEmpty()) {
                                    ExposedDropdownMenuDefaults.TrailingIcon(expanded = gameMenuExpanded)
                                }
                            },
                            colors = feedbackTextFieldColors()
                        )
                        ExposedDropdownMenu(
                            expanded = gameMenuExpanded,
                            onDismissRequest = { gameMenuExpanded = false },
                            modifier = Modifier.heightIn(max = 360.dp),
                            shape = neonShape(18.dp),
                            containerColor = MaterialTheme.colorScheme.surface.copy(alpha = 0.98f),
                            border = BorderStroke(
                                1.dp,
                                MaterialTheme.colorScheme.outline.copy(alpha = 0.38f)
                            )
                        ) {
                            if (filteredGames.isEmpty()) {
                                DropdownMenuItem(
                                    text = { Text(stringResource(R.string.feedback_game_no_matches)) },
                                    onClick = { gameMenuExpanded = false },
                                    enabled = false
                                )
                            } else {
                                filteredGames.forEach { game ->
                                    val isSelected = selectedGameId == game.titleId
                                    DropdownMenuItem(
                                        text = {
                                            Column {
                                                Text(
                                                    game.title,
                                                    color = if (isSelected) {
                                                        MaterialTheme.colorScheme.primary
                                                    } else {
                                                        MaterialTheme.colorScheme.onSurface
                                                    },
                                                    fontWeight = if (isSelected) FontWeight.SemiBold else FontWeight.Medium,
                                                    maxLines = 1,
                                                    overflow = TextOverflow.Ellipsis
                                                )
                                                Text(
                                                    game.titleId,
                                                    style = MaterialTheme.typography.bodySmall,
                                                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                                                    maxLines = 1,
                                                    overflow = TextOverflow.Ellipsis
                                                )
                                            }
                                        },
                                        trailingIcon = {
                                            if (isSelected) {
                                                Icon(
                                                    Icons.Rounded.Check,
                                                    contentDescription = null,
                                                    tint = MaterialTheme.colorScheme.primary
                                                )
                                            }
                                        },
                                        onClick = {
                                            selectedGameId = game.titleId
                                            manualGame = ""
                                            gameMenuExpanded = false
                                        }
                                    )
                                }
                            }
                        }
                    }
                    if (games.isEmpty() && !isLoadingLibrary) {
                        Text(
                            text = stringResource(R.string.feedback_game_library_empty),
                            style = MaterialTheme.typography.bodySmall,
                            color = MaterialTheme.colorScheme.onSurfaceVariant,
                            maxLines = 2,
                            overflow = TextOverflow.Ellipsis
                        )
                    }

                    OutlinedTextField(
                        value = message,
                        onValueChange = { message = it.take(FeedbackLimits.MAX_MESSAGE_LENGTH) },
                        modifier = Modifier
                            .fillMaxWidth()
                            .height(170.dp),
                        shape = neonShape(16.dp),
                        label = { Text(stringResource(R.string.feedback_message_label)) },
                        placeholder = { Text(stringResource(R.string.feedback_message_placeholder)) },
                        minLines = 4,
                        maxLines = 7,
                        colors = feedbackTextFieldColors(),
                        supportingText = {
                            Text(
                                stringResource(
                                    R.string.feedback_character_counter,
                                    message.length,
                                    FeedbackLimits.MAX_MESSAGE_LENGTH
                                )
                            )
                        }
                    )

                    Button(
                        shape = neonButtonShape(),
                        onClick = { showAttachmentSourceDialog = true },
                        enabled = !isInspectingAttachments && attachments.size < FeedbackLimits.MAX_ATTACHMENTS,
                        modifier = Modifier
                            .fillMaxWidth()
                            .height(48.dp)
                    ) {
                        Icon(Icons.Rounded.AttachFile, contentDescription = null, modifier = Modifier.size(20.dp))
                        Spacer(Modifier.size(8.dp))
                        Text(stringResource(R.string.feedback_add_attachment))
                    }
                    attachments.forEach { attachment ->
                        AttachmentRow(
                            attachment = attachment,
                            onRemove = { attachments.remove(attachment) }
                        )
                    }

                }
            }
        }
        item {
            Surface(
                modifier = Modifier
                    .fillMaxWidth()
                    .widthIn(max = 760.dp),
                shape = neonShape(20.dp),
                color = MaterialTheme.colorScheme.surface.copy(alpha = 0.14f),
                contentColor = MaterialTheme.colorScheme.onSurface,
                border = BorderStroke(
                    1.dp,
                    MaterialTheme.colorScheme.outline.copy(alpha = 0.16f)
                )
            ) {
                Column(
                    modifier = Modifier.padding(12.dp),
                    verticalArrangement = Arrangement.spacedBy(6.dp)
                ) {
                    Text(
                        text = stringResource(R.string.feedback_tip_title),
                        style = MaterialTheme.typography.titleSmall,
                        fontWeight = FontWeight.SemiBold
                    )
                    Text(
                        text = stringResource(R.string.feedback_tip_body),
                        style = MaterialTheme.typography.bodyMedium,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                    Text(
                        text = stringResource(
                            R.string.feedback_attachment_counter,
                            attachments.size,
                            FeedbackLimits.MAX_ATTACHMENTS
                        ),
                        style = MaterialTheme.typography.labelLarge,
                        color = MaterialTheme.colorScheme.primary,
                        fontWeight = FontWeight.SemiBold
                    )
                }
            }
        }
        item {
            Button(
                shape = neonButtonShape(),
                onClick = submitFeedback,
                enabled = !isQueueing && !isInspectingAttachments && FeedbackUploadScheduler.isConfigured,
                modifier = Modifier
                    .fillMaxWidth()
                    .widthIn(max = 760.dp)
                    .height(50.dp)
            ) {
                if (isQueueing) {
                    CircularProgressIndicator(
                        modifier = Modifier.size(18.dp),
                        strokeWidth = 2.dp,
                        color = MaterialTheme.colorScheme.onPrimary
                    )
                } else {
                    Icon(Icons.AutoMirrored.Rounded.Send, contentDescription = null, modifier = Modifier.size(19.dp))
                }
                Spacer(Modifier.size(8.dp))
                Text(stringResource(if (isQueueing) R.string.feedback_queueing else R.string.feedback_send))
            }
        }
    }

    if (showAttachmentSourceDialog) {
        AlertDialog(
            onDismissRequest = { showAttachmentSourceDialog = false },
            title = { Text(stringResource(R.string.feedback_attachment_source_title)) },
            text = {
                Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                    Button(
                        shape = neonButtonShape(),
                        onClick = {
                            showAttachmentSourceDialog = false
                            mediaPicker.launch(
                                PickVisualMediaRequest(ActivityResultContracts.PickVisualMedia.ImageAndVideo)
                            )
                        },
                        modifier = Modifier.fillMaxWidth()
                    ) {
                        Icon(Icons.Rounded.Collections, contentDescription = null)
                        Spacer(Modifier.size(8.dp))
                        Text(stringResource(R.string.feedback_add_media))
                    }
                    OutlinedButton(
                        shape = neonButtonShape(),
                        onClick = {
                            showAttachmentSourceDialog = false
                            filePicker.launch(arrayOf("*/*"))
                        },
                        modifier = Modifier.fillMaxWidth()
                    ) {
                        Icon(Icons.Rounded.UploadFile, contentDescription = null)
                        Spacer(Modifier.size(8.dp))
                        Text(stringResource(R.string.feedback_add_files))
                    }
                }
            },
            confirmButton = {},
            dismissButton = {
                TextButton(onClick = { showAttachmentSourceDialog = false }) {
                    Text(stringResource(R.string.common_cancel))
                }
            }
        )
    }
    if (showDiscardDialog) {
        AlertDialog(
            onDismissRequest = { showDiscardDialog = false },
            title = { Text(stringResource(R.string.feedback_discard_title)) },
            text = { Text(stringResource(R.string.feedback_discard_message)) },
            confirmButton = {
                TextButton(onClick = onBackClick) {
                    Text(stringResource(R.string.feedback_discard_action))
                }
            },
            dismissButton = {
                TextButton(onClick = { showDiscardDialog = false }) {
                    Text(stringResource(R.string.common_cancel))
                }
            }
        )
    }
}

@Composable
private fun FeedbackCard(
    title: String,
    modifier: Modifier = Modifier,
    content: @Composable ColumnScope.() -> Unit
) {
    Surface(
        modifier = modifier.fillMaxWidth(),
        shape = neonShape(22.dp),
        color = MaterialTheme.colorScheme.surface.copy(alpha = 0.58f),
        border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.56f))
    ) {
        Column(
            modifier = Modifier.padding(16.dp),
            verticalArrangement = Arrangement.spacedBy(10.dp)
        ) {
            Text(
                text = title,
                style = MaterialTheme.typography.titleMedium.copy(fontWeight = FontWeight.Bold)
            )
            content()
        }
    }
}

@Composable
private fun AttachmentRow(attachment: FeedbackAttachment, onRemove: () -> Unit) {
    Surface(
        modifier = Modifier.fillMaxWidth(),
        shape = neonShape(16.dp),
        color = MaterialTheme.colorScheme.surfaceContainerHigh
    ) {
        Row(
            modifier = Modifier.padding(start = 14.dp, top = 8.dp, bottom = 8.dp, end = 6.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            Icon(Icons.Rounded.AttachFile, contentDescription = null, tint = MaterialTheme.colorScheme.primary)
            Column(modifier = Modifier.weight(1f).padding(horizontal = 10.dp)) {
                Text(attachment.displayName, maxLines = 1, overflow = TextOverflow.Ellipsis)
                Text(
                    listOfNotNull(
                        attachment.mimeType,
                        attachment.sizeBytes?.let(::formatFileSize)
                    ).joinToString(" · "),
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis
                )
            }
            IconButton(onClick = onRemove) {
                Icon(
                    Icons.Rounded.Close,
                    contentDescription = stringResource(R.string.feedback_remove_attachment)
                )
            }
        }
    }
}

@Composable
private fun FeedbackConfigurationWarning(modifier: Modifier = Modifier) {
    Surface(
        modifier = modifier.fillMaxWidth(),
        shape = neonShape(16.dp),
        color = MaterialTheme.colorScheme.errorContainer
    ) {
        Text(
            text = stringResource(R.string.feedback_not_configured),
            modifier = Modifier.padding(16.dp),
            color = MaterialTheme.colorScheme.onErrorContainer
        )
    }
}

private fun formatFileSize(bytes: Long): String {
    val mebibytes = bytes / (1024.0 * 1024.0)
    return if (mebibytes >= 1.0) {
        String.format(Locale.getDefault(), "%.1f MB", mebibytes)
    } else {
        String.format(Locale.getDefault(), "%.0f KB", bytes / 1024.0)
    }
}

@Composable
private fun feedbackTextFieldColors() = OutlinedTextFieldDefaults.colors(
    focusedContainerColor = MaterialTheme.colorScheme.surface.copy(alpha = 0.42f),
    unfocusedContainerColor = MaterialTheme.colorScheme.surface.copy(alpha = 0.28f),
    focusedBorderColor = MaterialTheme.colorScheme.primary.copy(alpha = 0.78f),
    unfocusedBorderColor = MaterialTheme.colorScheme.outline.copy(alpha = 0.38f),
    cursorColor = MaterialTheme.colorScheme.primary
)

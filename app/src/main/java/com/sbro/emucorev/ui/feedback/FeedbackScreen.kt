@file:OptIn(androidx.compose.foundation.layout.ExperimentalLayoutApi::class)

package com.sbro.emucorev.ui.feedback

import android.net.Uri
import android.widget.Toast
import androidx.activity.compose.BackHandler
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.PickVisualMediaRequest
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.FlowRow
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
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.rounded.Send
import androidx.compose.material.icons.rounded.AttachFile
import androidx.compose.material.icons.rounded.Close
import androidx.compose.material.icons.rounded.Collections
import androidx.compose.material.icons.rounded.UploadFile
import androidx.compose.material3.AlertDialog
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
fun FeedbackScreen(onBackClick: () -> Unit) {
    val context = LocalContext.current
    val scope = rememberCoroutineScope()
    val attachments = remember { mutableStateListOf<FeedbackAttachment>() }
    var category by rememberSaveable { mutableStateOf("compatibility") }
    var selectedGameId by rememberSaveable { mutableStateOf<String?>(null) }
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
    }
    val selectedGame = remember(games, selectedGameId) {
        selectedGameId?.let { id -> games.firstOrNull { it.titleId == id } }
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
        modifier = Modifier.fillMaxSize(),
        contentPadding = PaddingValues(
            start = 8.dp,
            top = WindowInsets.statusBarsIgnoringVisibility.asPaddingValues().calculateTopPadding(),
            end = 8.dp,
            bottom = WindowInsets.navigationBars.asPaddingValues().calculateBottomPadding() + 18.dp
        ),
        verticalArrangement = Arrangement.spacedBy(10.dp),
        horizontalAlignment = Alignment.CenterHorizontally
    ) {
        item {
            ScreenTopBar(
                title = stringResource(R.string.feedback_title),
                subtitle = stringResource(R.string.feedback_subtitle),
                onBackClick = requestBack,
                modifier = Modifier.fillMaxWidth().widthIn(max = 780.dp),
                titleMaxLines = 2,
                subtitleMaxLines = 2
            )
        }
        if (!FeedbackUploadScheduler.isConfigured) {
            item { FeedbackConfigurationWarning(Modifier.widthIn(max = 780.dp)) }
        }
        item {
            FeedbackCard(
                title = stringResource(R.string.feedback_category_title),
                modifier = Modifier.widthIn(max = 780.dp)
            ) {
                FlowRow(
                    horizontalArrangement = Arrangement.spacedBy(8.dp),
                    verticalArrangement = Arrangement.spacedBy(6.dp)
                ) {
                    categories.forEach { item ->
                        FilterChip(
                            selected = category == item.id,
                            onClick = { category = item.id },
                            label = { Text(item.label) }
                        )
                    }
                }
            }
        }
        item {
            FeedbackCard(
                title = stringResource(R.string.feedback_details_title),
                modifier = Modifier.widthIn(max = 780.dp)
            ) {
                OutlinedTextField(
                    value = selectedGame?.let { "${it.title} · ${it.titleId}" } ?: manualGame,
                    onValueChange = {
                        selectedGameId = null
                        manualGame = it.take(FeedbackLimits.MAX_GAME_LENGTH)
                    },
                    modifier = Modifier.fillMaxWidth(),
                    label = { Text(stringResource(R.string.feedback_game_label)) },
                    placeholder = { Text(stringResource(R.string.feedback_game_placeholder)) },
                    trailingIcon = if (selectedGame != null) {
                        {
                            IconButton(onClick = {
                                selectedGameId = null
                                manualGame = ""
                            }) {
                                Icon(
                                    Icons.Rounded.Close,
                                    contentDescription = stringResource(R.string.feedback_game_clear_selection)
                                )
                            }
                        }
                    } else {
                        null
                    },
                    singleLine = true,
                    shape = RoundedCornerShape(16.dp)
                )
                if (games.isNotEmpty()) {
                    Text(
                        text = stringResource(R.string.feedback_game_label),
                        style = MaterialTheme.typography.labelLarge,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                    FlowRow(
                        horizontalArrangement = Arrangement.spacedBy(8.dp),
                        verticalArrangement = Arrangement.spacedBy(6.dp)
                    ) {
                        games.take(16).forEach { game ->
                            FilterChip(
                                selected = selectedGameId == game.titleId,
                                onClick = {
                                    selectedGameId = if (selectedGameId == game.titleId) null else game.titleId
                                    manualGame = ""
                                },
                                label = {
                                    Text(
                                        game.title,
                                        maxLines = 1,
                                        overflow = TextOverflow.Ellipsis
                                    )
                                }
                            )
                        }
                    }
                } else {
                    Text(
                        text = stringResource(R.string.feedback_game_library_empty),
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                }
                OutlinedTextField(
                    value = message,
                    onValueChange = { message = it.take(FeedbackLimits.MAX_MESSAGE_LENGTH) },
                    modifier = Modifier.fillMaxWidth().height(180.dp),
                    label = { Text(stringResource(R.string.feedback_message_label)) },
                    placeholder = { Text(stringResource(R.string.feedback_message_placeholder)) },
                    minLines = 4,
                    maxLines = 8,
                    shape = RoundedCornerShape(16.dp),
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
            }
        }
        item {
            FeedbackCard(
                title = stringResource(R.string.feedback_attachments_title),
                modifier = Modifier.widthIn(max = 780.dp)
            ) {
                Text(
                    text = stringResource(
                        R.string.feedback_attachments_help,
                        FeedbackLimits.MAX_ATTACHMENTS,
                        FeedbackLimits.MAX_ATTACHMENT_BYTES / 1024 / 1024,
                        FeedbackLimits.MAX_TOTAL_BYTES / 1024 / 1024
                    ),
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
                Button(
                    onClick = { showAttachmentSourceDialog = true },
                    enabled = !isInspectingAttachments && attachments.size < FeedbackLimits.MAX_ATTACHMENTS,
                    modifier = Modifier.fillMaxWidth()
                ) {
                    Icon(Icons.Rounded.AttachFile, contentDescription = null)
                    Spacer(Modifier.size(8.dp))
                    Text(stringResource(R.string.feedback_add_attachment))
                }
                attachments.forEach { attachment ->
                    AttachmentRow(attachment = attachment, onRemove = { attachments.remove(attachment) })
                }
            }
        }
        item {
            FeedbackCard(
                title = stringResource(R.string.feedback_tip_title),
                modifier = Modifier.widthIn(max = 780.dp)
            ) {
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
                    color = MaterialTheme.colorScheme.primary
                )
            }
        }
        item {
            Button(
                onClick = submitFeedback,
                enabled = !isQueueing && !isInspectingAttachments && FeedbackUploadScheduler.isConfigured,
                modifier = Modifier.fillMaxWidth().widthIn(max = 780.dp).height(52.dp)
            ) {
                if (isQueueing) {
                    CircularProgressIndicator(
                        modifier = Modifier.size(18.dp),
                        strokeWidth = 2.dp,
                        color = MaterialTheme.colorScheme.onPrimary
                    )
                } else {
                    Icon(Icons.AutoMirrored.Rounded.Send, contentDescription = null)
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
        shape = RoundedCornerShape(22.dp),
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
        shape = RoundedCornerShape(14.dp),
        color = MaterialTheme.colorScheme.surfaceContainerHigh
    ) {
        Row(
            modifier = Modifier.padding(start = 12.dp, top = 8.dp, bottom = 8.dp, end = 4.dp),
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
        shape = RoundedCornerShape(18.dp),
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

package com.sbro.emucorev.ui.library

import android.widget.Toast
import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.expandVertically
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.shrinkVertically
import androidx.compose.foundation.combinedClickable
import androidx.compose.foundation.background
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.asPaddingValues
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.navigationBarsPadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.statusBarsIgnoringVisibility
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.rounded.ViewList
import androidx.compose.material.icons.rounded.Close
import androidx.compose.material.icons.rounded.Delete
import androidx.compose.material.icons.rounded.FolderOpen
import androidx.compose.material.icons.rounded.Menu
import androidx.compose.material.icons.rounded.Refresh
import androidx.compose.material.icons.rounded.Search
import androidx.compose.material.icons.rounded.ViewModule
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.OutlinedTextFieldDefaults
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.pluralStringResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.lifecycle.viewmodel.compose.viewModel
import com.sbro.emucorev.R
import com.sbro.emucorev.ui.common.LocalImage
import com.sbro.emucorev.ui.common.PremiumLoadingAnimation
import com.sbro.emucorev.ui.common.rememberDebouncedClick
import com.sbro.emucorev.ui.theme.CardContentPadding
import com.sbro.emucorev.ui.theme.CompactCardContentPadding
import com.sbro.emucorev.ui.theme.ScreenContentBottomPadding
import com.sbro.emucorev.ui.theme.ScreenHorizontalPadding
import com.sbro.emucorev.ui.theme.ScreenTopInsetOffset

private enum class LibraryLayoutMode {
    LIST,
    GRID
}

@OptIn(ExperimentalLayoutApi::class)
@Composable
fun LibraryScreen(
    onLaunchGame: (String) -> Unit,
    onMenuClick: (() -> Unit)? = null,
    viewModel: LibraryViewModel = viewModel()
) {
    val uiState by viewModel.uiState.collectAsState()
    val context = LocalContext.current
    val refreshClick = rememberDebouncedClick(onClick = viewModel::refresh)
    var layoutMode by rememberSaveable { mutableStateOf(LibraryLayoutMode.LIST) }
    var searchExpanded by rememberSaveable { mutableStateOf(false) }
    var deleteGameId by remember { mutableStateOf<String?>(null) }
    val topInset = WindowInsets.statusBarsIgnoringVisibility.asPaddingValues().calculateTopPadding() + ScreenTopInsetOffset
    val gameCountSubtitle = pluralStringResource(
        R.plurals.library_game_count,
        uiState.items.size,
        uiState.items.size
    )
    val deleteGameLabel = stringResource(R.string.detail_delete_game)
    val deleteGameConfirmTitle = stringResource(R.string.detail_delete_game_confirm_title)
    val deleteGameConfirmBody = stringResource(R.string.detail_delete_game_confirm_body)
    val deleteGameFailedMessage = stringResource(R.string.detail_delete_game_failed)

    LazyColumn(
        modifier = Modifier
            .fillMaxSize()
            .background(MaterialTheme.colorScheme.background)
            .navigationBarsPadding(),
        contentPadding = androidx.compose.foundation.layout.PaddingValues(
            start = ScreenHorizontalPadding,
            end = ScreenHorizontalPadding,
            top = topInset,
            bottom = ScreenContentBottomPadding
        ),
        verticalArrangement = Arrangement.spacedBy(14.dp)
    ) {
        item {
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically
            ) {
                Row(
                    modifier = Modifier.weight(1f),
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    if (onMenuClick != null) {
                        Surface(
                            modifier = Modifier.padding(end = 14.dp),
                            shape = RoundedCornerShape(14.dp),
                            color = MaterialTheme.colorScheme.surface,
                            tonalElevation = 3.dp,
                            shadowElevation = 5.dp,
                            border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.55f)),
                            onClick = rememberDebouncedClick(onClick = onMenuClick)
                        ) {
                            Box(
                                modifier = Modifier.padding(horizontal = CompactCardContentPadding, vertical = CompactCardContentPadding),
                                contentAlignment = Alignment.Center
                            ) {
                                Icon(
                                    imageVector = Icons.Rounded.Menu,
                                    contentDescription = null,
                                    tint = MaterialTheme.colorScheme.onSurface
                                )
                            }
                        }
                    }
                    Column(modifier = Modifier.weight(1f)) {
                        Text(
                            text = "EmuCoreV",
                            style = MaterialTheme.typography.headlineSmall.copy(fontWeight = FontWeight.Bold),
                            color = MaterialTheme.colorScheme.onBackground
                        )
                        Text(
                            text = gameCountSubtitle,
                            style = MaterialTheme.typography.bodySmall,
                            color = MaterialTheme.colorScheme.onSurfaceVariant
                        )
                    }
                }
                Row(verticalAlignment = Alignment.CenterVertically) {
                    IconButton(
                        onClick = {
                            if (searchExpanded) {
                                searchExpanded = false
                                viewModel.updateQuery("")
                            } else {
                                searchExpanded = true
                            }
                        }
                    ) {
                        Icon(
                            imageVector = if (searchExpanded) Icons.Rounded.Close else Icons.Rounded.Search,
                            contentDescription = stringResource(R.string.library_search_hint),
                            tint = MaterialTheme.colorScheme.onSurfaceVariant
                        )
                    }
                    IconButton(onClick = { layoutMode = if (layoutMode == LibraryLayoutMode.LIST) LibraryLayoutMode.GRID else LibraryLayoutMode.LIST }) {
                        Icon(
                            imageVector = if (layoutMode == LibraryLayoutMode.LIST) Icons.Rounded.ViewModule else Icons.AutoMirrored.Rounded.ViewList,
                            contentDescription = null,
                            tint = MaterialTheme.colorScheme.onSurfaceVariant
                        )
                    }
                    IconButton(onClick = refreshClick) {
                        Icon(
                            imageVector = Icons.Rounded.Refresh,
                            contentDescription = stringResource(R.string.library_refresh),
                            tint = MaterialTheme.colorScheme.onSurfaceVariant
                        )
                    }
                }
            }
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
                    placeholder = { Text(stringResource(R.string.library_search_hint)) },
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
                        .padding(vertical = 48.dp),
                    contentAlignment = Alignment.Center
                ) {
                    PremiumLoadingAnimation(size = 64.dp)
                }
            }
        } else if (uiState.items.isEmpty()) {
            item {
                Box(
                    modifier = Modifier
                        .fillMaxWidth()
                        .heightIn(min = 360.dp),
                    contentAlignment = Alignment.Center
                ) {
                    Column(
                        modifier = Modifier.widthIn(max = 520.dp),
                        horizontalAlignment = Alignment.CenterHorizontally,
                        verticalArrangement = Arrangement.spacedBy(18.dp)
                    ) {
                        Text(
                            text = stringResource(R.string.library_empty_title),
                            style = MaterialTheme.typography.titleLarge.copy(fontWeight = FontWeight.Bold),
                            color = MaterialTheme.colorScheme.onBackground,
                            textAlign = TextAlign.Center
                        )
                        Text(
                            text = stringResource(R.string.library_empty_body),
                            color = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.82f),
                            textAlign = TextAlign.Center
                        )
                        Button(onClick = refreshClick) {
                            Icon(Icons.Rounded.FolderOpen, contentDescription = null)
                            Text(stringResource(R.string.library_refresh))
                        }
                    }
                }
            }
        } else if (layoutMode == LibraryLayoutMode.LIST) {
            items(uiState.items, key = { it.titleId }) { game ->
                val selectGameClick = rememberDebouncedClick { onLaunchGame(game.titleId) }
                val shape = RoundedCornerShape(24.dp)
                var menuExpanded by remember(game.titleId) { mutableStateOf(false) }
                Box {
                    Surface(
                        modifier = Modifier
                            .fillMaxWidth()
                            .clip(shape)
                            .combinedClickable(
                                onClick = selectGameClick,
                                onLongClick = { menuExpanded = true }
                            ),
                        shape = shape,
                        color = MaterialTheme.colorScheme.surface,
                        tonalElevation = 3.dp,
                        shadowElevation = 8.dp,
                        border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.58f))
                    ) {
                        Row(
                            modifier = Modifier.padding(CardContentPadding),
                            horizontalArrangement = Arrangement.spacedBy(14.dp),
                            verticalAlignment = Alignment.Top
                        ) {
                            LibraryGameArtwork(
                                path = game.iconPath,
                                title = game.title,
                                modifier = Modifier.width(74.dp)
                            )
                            Column(
                                modifier = Modifier.weight(1f),
                                verticalArrangement = Arrangement.spacedBy(6.dp)
                            ) {
                                Text(
                                    text = game.title,
                                    style = MaterialTheme.typography.titleMedium.copy(fontWeight = FontWeight.Bold),
                                    maxLines = 2,
                                    overflow = TextOverflow.Ellipsis
                                )
                                Text(
                                    text = game.titleId,
                                    color = MaterialTheme.colorScheme.primary
                                )
                                Text(
                                    text = game.version ?: stringResource(R.string.common_not_available),
                                    color = MaterialTheme.colorScheme.onSurfaceVariant
                                )
                            }
                        }
                    }
                    DropdownMenu(
                        expanded = menuExpanded,
                        onDismissRequest = { menuExpanded = false }
                    ) {
                        DropdownMenuItem(
                            text = { Text(deleteGameLabel) },
                            onClick = {
                                deleteGameId = game.titleId
                                menuExpanded = false
                            },
                            leadingIcon = {
                                Icon(
                                    imageVector = Icons.Rounded.Delete,
                                    contentDescription = null
                                )
                            }
                        )
                    }
                }
            }
        } else {
            items(
                items = uiState.items.chunked(3),
                key = { row -> row.firstOrNull()?.titleId ?: row.hashCode().toString() }
            ) { rowItems ->
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.spacedBy(12.dp)
                ) {
                    rowItems.forEach { game ->
                        val selectGameClick = rememberDebouncedClick { onLaunchGame(game.titleId) }
                        val shape = RoundedCornerShape(24.dp)
                        var menuExpanded by remember(game.titleId) { mutableStateOf(false) }
                        Box(modifier = Modifier.weight(1f)) {
                            Surface(
                                modifier = Modifier
                                    .fillMaxWidth()
                                    .clip(shape)
                                    .combinedClickable(
                                        onClick = selectGameClick,
                                        onLongClick = { menuExpanded = true }
                                    ),
                                shape = shape,
                                color = MaterialTheme.colorScheme.surface,
                                tonalElevation = 3.dp,
                                shadowElevation = 8.dp,
                                border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.58f))
                            ) {
                                Column(
                                    modifier = Modifier.padding(12.dp),
                                    verticalArrangement = Arrangement.spacedBy(10.dp)
                                ) {
                                    LibraryGameArtwork(
                                        path = game.iconPath,
                                        title = game.title,
                                        modifier = Modifier.fillMaxWidth()
                                    )
                                    Column(verticalArrangement = Arrangement.spacedBy(4.dp)) {
                                        Text(
                                            text = game.title,
                                            style = MaterialTheme.typography.titleSmall.copy(fontWeight = FontWeight.Bold),
                                            maxLines = 2,
                                            overflow = TextOverflow.Ellipsis
                                        )
                                        Text(
                                            text = game.titleId,
                                            style = MaterialTheme.typography.labelMedium,
                                            color = MaterialTheme.colorScheme.primary,
                                            maxLines = 1,
                                            overflow = TextOverflow.Ellipsis
                                        )
                                    }
                                }
                            }
                            DropdownMenu(
                                expanded = menuExpanded,
                                onDismissRequest = { menuExpanded = false }
                            ) {
                                DropdownMenuItem(
                                    text = { Text(deleteGameLabel) },
                                    onClick = {
                                        deleteGameId = game.titleId
                                        menuExpanded = false
                                    },
                                    leadingIcon = {
                                        Icon(
                                            imageVector = Icons.Rounded.Delete,
                                            contentDescription = null
                                        )
                                    }
                                )
                            }
                        }
                    }
                    repeat(3 - rowItems.size) {
                        Spacer(modifier = Modifier.weight(1f))
                    }
                }
            }
        }
    }

    if (deleteGameId != null) {
        AlertDialog(
            onDismissRequest = { deleteGameId = null },
            title = { Text(deleteGameConfirmTitle) },
            text = { Text(deleteGameConfirmBody) },
            confirmButton = {
                TextButton(
                    onClick = {
                        val targetGameId = deleteGameId
                        if (targetGameId != null) {
                            viewModel.deleteInstalledGame(targetGameId) { deleted ->
                                if (!deleted) {
                                    Toast.makeText(context, deleteGameFailedMessage, Toast.LENGTH_SHORT).show()
                                }
                            }
                        }
                        deleteGameId = null
                    }
                ) {
                    Text(deleteGameLabel)
                }
            },
            dismissButton = {
                TextButton(onClick = { deleteGameId = null }) {
                    Text(stringResource(R.string.install_dialog_close))
                }
            }
        )
    }
}

@Composable
private fun LibraryGameArtwork(
    path: String?,
    title: String,
    modifier: Modifier = Modifier
) {
    Surface(
        modifier = modifier,
        shape = RoundedCornerShape(18.dp),
        color = MaterialTheme.colorScheme.surfaceContainerHigh,
        border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.5f))
    ) {
        LocalImage(
            path = path,
            contentDescription = title,
            fallbackLabel = title,
            modifier = Modifier
                .fillMaxWidth()
                .aspectRatio(2f / 3f)
        )
    }
}

@file:OptIn(androidx.compose.foundation.layout.ExperimentalLayoutApi::class)

package com.sbro.emucorev.ui.cheats

import android.widget.Toast
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.asPaddingValues
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.navigationBarsPadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.statusBarsIgnoringVisibility
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.LazyRow
import androidx.compose.foundation.lazy.items
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.rounded.ArrowBack
import androidx.compose.material.icons.automirrored.rounded.ArrowForward
import androidx.compose.material.icons.rounded.AutoFixHigh
import androidx.compose.material.icons.rounded.Close
import androidx.compose.material.icons.rounded.CloudDownload
import androidx.compose.material.icons.rounded.DeleteOutline
import androidx.compose.material.icons.rounded.Folder
import androidx.compose.material.icons.rounded.FolderOpen
import androidx.compose.material.icons.rounded.Refresh
import androidx.compose.material.icons.rounded.Search
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Surface
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
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
import com.sbro.emucorev.core.VitaCheatEntry
import com.sbro.emucorev.data.InstalledVitaGame
import com.sbro.emucorev.ui.common.LocalImage
import com.sbro.emucorev.ui.common.ScreenTopBar
import com.sbro.emucorev.ui.theme.ScreenContentBottomPadding
import com.sbro.emucorev.ui.theme.ScreenHorizontalPadding
import com.sbro.emucorev.ui.theme.neon.neonShape

@Composable
fun CheatManagerScreen(
    onBackClick: () -> Unit,
    onMenuClick: (() -> Unit)? = null,
    viewModel: CheatManagerViewModel = viewModel()
) {
    val state by viewModel.state.collectAsState()
    val context = LocalContext.current
    val topInset = WindowInsets.statusBarsIgnoringVisibility.asPaddingValues().calculateTopPadding()
    var deleteConfirmVisible by remember { mutableStateOf(false) }
    var cheatSearchVisible by remember { mutableStateOf(false) }
    var cheatSearchQuery by remember { mutableStateOf("") }
    var selectedCategory by remember { mutableStateOf<CheatCategory?>(null) }

    val importLauncher = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        if (uri != null) viewModel.importFromUri(uri)
    }

    LaunchedEffect(state.messageRes) {
        state.messageRes?.let { message ->
            Toast.makeText(context, message, Toast.LENGTH_SHORT).show()
            viewModel.consumeMessage()
        }
    }

    val selectedGame = state.games.firstOrNull { it.titleId == state.selectedTitleId }
    val categoryGroups = remember(state.snapshot.cheats) { groupCheatEntries(state.snapshot.cheats) }
    val searchResults = remember(state.snapshot.cheats, cheatSearchQuery) {
        if (cheatSearchQuery.isBlank()) state.snapshot.cheats
        else state.snapshot.cheats.filter { it.name.contains(cheatSearchQuery, ignoreCase = true) }
    }
    val visibleBlocks = when {
        cheatSearchVisible -> searchResults
        selectedCategory != null -> state.snapshot.cheats.filter { it.category() == selectedCategory }
        else -> emptyList()
    }
    val catalogForGame = state.catalog.filter { it.titleId == state.selectedTitleId }

    LazyColumn(
        modifier = Modifier
            .fillMaxSize()
            .background(MaterialTheme.colorScheme.background)
            .navigationBarsPadding(),
        contentPadding = PaddingValues(
            top = topInset,
            bottom = ScreenContentBottomPadding
        ),
        verticalArrangement = Arrangement.spacedBy(12.dp)
    ) {
        item {
            ScreenTopBar(
                title = stringResource(R.string.nav_cheat_manager),
                showNeonDivider = true,
                onBackClick = onBackClick,
                onMenuClick = onMenuClick,
                modifier = Modifier.padding(horizontal = ScreenHorizontalPadding),
                actions = {
                    IconButton(onClick = { viewModel.refreshCheats() }) {
                        Icon(
                            imageVector = Icons.Rounded.Refresh,
                            contentDescription = stringResource(R.string.library_refresh),
                            tint = MaterialTheme.colorScheme.onSurfaceVariant
                        )
                    }
                }
            )
        }

        if (state.games.isEmpty()) {
            item {
                Box(
                    modifier = Modifier
                        .fillMaxWidth()
                        .heightIn(min = 360.dp),
                    contentAlignment = Alignment.Center
                ) {
                    Column(
                        horizontalAlignment = Alignment.CenterHorizontally,
                        verticalArrangement = Arrangement.spacedBy(12.dp)
                    ) {
                        Icon(
                            imageVector = Icons.Rounded.AutoFixHigh,
                            contentDescription = null,
                            modifier = Modifier.size(48.dp),
                            tint = MaterialTheme.colorScheme.primary
                        )
                        Text(
                            text = stringResource(R.string.cheat_manager_empty_title),
                            style = MaterialTheme.typography.titleLarge.copy(fontWeight = FontWeight.Bold)
                        )
                    }
                }
            }
            return@LazyColumn
        }

        item {
            Surface(
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(horizontal = ScreenHorizontalPadding),
                shape = neonShape(22.dp),
                color = MaterialTheme.colorScheme.surface,
                border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant)
            ) {
                Column(
                    modifier = Modifier.padding(16.dp),
                    verticalArrangement = Arrangement.spacedBy(14.dp)
                ) {
                    Row(
                        verticalAlignment = Alignment.CenterVertically,
                        horizontalArrangement = Arrangement.spacedBy(16.dp)
                    ) {
                        Column(modifier = Modifier.weight(1f)) {
                            Text(
                                text = stringResource(R.string.settings_enable_cheats),
                                style = MaterialTheme.typography.titleMedium,
                                fontWeight = FontWeight.SemiBold
                            )
                            Text(
                                text = stringResource(R.string.cheat_manager_enable_desc),
                                style = MaterialTheme.typography.bodySmall,
                                color = MaterialTheme.colorScheme.onSurfaceVariant
                            )
                        }
                        Switch(
                            checked = state.snapshot.masterEnabled,
                            onCheckedChange = viewModel::setMasterEnabled
                        )
                    }
                    OutlinedButton(
                        shape = neonShape(16.dp),
                        onClick = { importLauncher.launch(arrayOf("*/*")) },
                        enabled = !state.busy
                    ) {
                        Icon(Icons.Rounded.FolderOpen, contentDescription = null, modifier = Modifier.size(18.dp))
                        Spacer(modifier = Modifier.width(8.dp))
                        Text(stringResource(R.string.emulation_cheats_import))
                    }
                }
            }
        }

        item {
            CheatGamePicker(
                games = state.games,
                selectedTitleId = state.selectedTitleId,
                selectedGame = selectedGame,
                onSelect = {
                    selectedCategory = null
                    cheatSearchVisible = false
                    cheatSearchQuery = ""
                    viewModel.selectGame(it)
                }
            )
        }

        item {
            Surface(
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(horizontal = ScreenHorizontalPadding),
                shape = neonShape(18.dp),
                color = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.4f)
            ) {
                Row(
                    modifier = Modifier
                        .fillMaxWidth()
                        .padding(14.dp),
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.spacedBy(10.dp)
                ) {
                    Text(
                        text = selectedGame?.title ?: state.selectedTitleId,
                        style = MaterialTheme.typography.bodyLarge.copy(fontWeight = FontWeight.Medium),
                        modifier = Modifier.weight(1f),
                        maxLines = 1,
                        overflow = TextOverflow.Ellipsis
                    )
                    Text(
                        text = state.selectedTitleId,
                        style = MaterialTheme.typography.labelLarge,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                }
            }
        }

        item {
            Surface(
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(horizontal = ScreenHorizontalPadding),
                shape = neonShape(22.dp),
                color = MaterialTheme.colorScheme.surface,
                border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant)
            ) {
                Column(
                    modifier = Modifier.padding(16.dp),
                    verticalArrangement = Arrangement.spacedBy(12.dp)
                ) {
                    Text(
                        text = stringResource(R.string.cheat_catalog_title),
                        style = MaterialTheme.typography.titleMedium,
                        fontWeight = FontWeight.SemiBold
                    )
                    Text(
                        text = stringResource(R.string.cheat_catalog_description),
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                    when {
                        state.catalogLoading -> Box(
                            modifier = Modifier
                                .fillMaxWidth()
                                .padding(vertical = 8.dp),
                            contentAlignment = Alignment.Center
                        ) {
                            CircularProgressIndicator(modifier = Modifier.size(26.dp))
                        }

                        catalogForGame.isEmpty() -> {
                            Text(
                                text = stringResource(R.string.cheat_catalog_empty),
                                style = MaterialTheme.typography.bodyMedium
                            )
                            if (state.catalogFailed) {
                                TextButton(onClick = { viewModel.loadCatalog(force = true) }) {
                                    Text(stringResource(R.string.cheat_catalog_retry))
                                }
                            }
                        }
                    }
                }
            }
        }

        if (!state.catalogLoading && catalogForGame.isNotEmpty()) {
            items(catalogForGame, key = { it.id }) { entry ->
                CheatCatalogCard(
                    entry = entry,
                    installed = state.installed,
                    busy = state.busy,
                    onDownload = { viewModel.download(entry) }
                )
            }
        }

        item {
            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(horizontal = ScreenHorizontalPadding),
                verticalAlignment = Alignment.CenterVertically
            ) {
                Text(
                    text = stringResource(R.string.cheat_manager_installed),
                    modifier = Modifier.weight(1f),
                    style = MaterialTheme.typography.titleLarge,
                    fontWeight = FontWeight.Bold
                )
                IconButton(
                    onClick = {
                        cheatSearchVisible = !cheatSearchVisible
                        selectedCategory = null
                        if (!cheatSearchVisible) cheatSearchQuery = ""
                    }
                ) {
                    Icon(
                        imageVector = if (cheatSearchVisible) Icons.Rounded.Close else Icons.Rounded.Search,
                        contentDescription = stringResource(R.string.cheat_manager_search)
                    )
                }
            }
        }

        if (state.snapshot.cheats.isEmpty()) {
            item {
                Surface(
                    modifier = Modifier
                        .fillMaxWidth()
                        .padding(horizontal = ScreenHorizontalPadding),
                    shape = neonShape(18.dp),
                    color = MaterialTheme.colorScheme.surface,
                    border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.72f))
                ) {
                    Column(
                        modifier = Modifier.padding(16.dp),
                        verticalArrangement = Arrangement.spacedBy(4.dp)
                    ) {
                        Text(
                            text = stringResource(R.string.cheat_manager_empty_title),
                            style = MaterialTheme.typography.bodyLarge
                        )
                        Text(
                            text = stringResource(R.string.cheat_manager_empty_body),
                            style = MaterialTheme.typography.bodySmall,
                            color = MaterialTheme.colorScheme.onSurfaceVariant
                        )
                    }
                }
            }
        } else if (cheatSearchVisible) {
            item {
                OutlinedTextField(
                    value = cheatSearchQuery,
                    onValueChange = { cheatSearchQuery = it },
                    modifier = Modifier
                        .fillMaxWidth()
                        .padding(horizontal = ScreenHorizontalPadding),
                    placeholder = { Text(stringResource(R.string.cheat_manager_search)) },
                    leadingIcon = { Icon(Icons.Rounded.Search, contentDescription = null) },
                    trailingIcon = if (cheatSearchQuery.isNotEmpty()) {
                        {
                            IconButton(onClick = { cheatSearchQuery = "" }) {
                                Icon(Icons.Rounded.Close, contentDescription = null)
                            }
                        }
                    } else null,
                    singleLine = true,
                    shape = neonShape(18.dp)
                )
            }
            item {
                CheatListHeader(
                    title = stringResource(R.string.cheat_manager_search),
                    count = visibleBlocks.size
                )
            }
            if (visibleBlocks.isEmpty()) {
                item {
                    CheatSearchEmptyState()
                }
            } else {
                items(visibleBlocks, key = { cheatKey(it) }) { cheat ->
                    CheatToggleCard(
                        entry = cheat,
                        masterEnabled = state.snapshot.masterEnabled,
                        onToggle = { enabled -> viewModel.setCheatEnabled(indexOf(state.snapshot.cheats, cheat), enabled) }
                    )
                }
            }
        } else if (selectedCategory == null) {
            item {
                CheatCategoryBrowserHeader(
                    selectedCategory = null,
                    count = state.snapshot.cheats.size,
                    onBack = { }
                )
            }
            items(categoryGroups, key = { "cheat-category-${it.first.name}" }) { (category, entries) ->
                CheatCategoryCard(
                    category = category,
                    count = entries.size,
                    onClick = { selectedCategory = category }
                )
            }
        } else {
            val category = selectedCategory
            item {
                CheatCategoryBrowserHeader(
                    selectedCategory = category,
                    count = visibleBlocks.size,
                    onBack = { selectedCategory = null }
                )
            }
            items(visibleBlocks, key = { cheatKey(it) }) { cheat ->
                CheatToggleCard(
                    entry = cheat,
                    masterEnabled = state.snapshot.masterEnabled,
                    onToggle = { enabled -> viewModel.setCheatEnabled(indexOf(state.snapshot.cheats, cheat), enabled) }
                )
            }
        }

        item {
            OutlinedButton(
                shape = neonShape(16.dp),
                onClick = { deleteConfirmVisible = true },
                enabled = state.installed && !state.busy,
                modifier = Modifier.padding(horizontal = ScreenHorizontalPadding)
            ) {
                Icon(Icons.Rounded.DeleteOutline, contentDescription = null, modifier = Modifier.size(18.dp))
                Spacer(modifier = Modifier.width(8.dp))
                Text(stringResource(R.string.cheat_manager_delete))
            }
        }
    }

    if (deleteConfirmVisible) {
        AlertDialog(
            onDismissRequest = { deleteConfirmVisible = false },
            title = { Text(stringResource(R.string.cheat_manager_delete_confirm_title)) },
            text = { Text(stringResource(R.string.cheat_manager_delete_confirm_body, state.selectedTitleId)) },
            confirmButton = {
                TextButton(
                    onClick = {
                        deleteConfirmVisible = false
                        viewModel.deleteCheats()
                    }
                ) {
                    Text(stringResource(R.string.cheat_manager_delete))
                }
            },
            dismissButton = {
                TextButton(onClick = { deleteConfirmVisible = false }) {
                    Text(stringResource(android.R.string.cancel))
                }
            }
        )
    }
}

private fun cheatKey(entry: VitaCheatEntry): String = entry.name + entry.codes.hashCode()

private fun indexOf(entries: List<VitaCheatEntry>, entry: VitaCheatEntry): Int = entries.indexOf(entry)

@Composable
private fun CheatListHeader(title: String, count: Int) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .padding(horizontal = ScreenHorizontalPadding),
        verticalAlignment = Alignment.CenterVertically
    ) {
        Text(
            text = title,
            modifier = Modifier.weight(1f),
            style = MaterialTheme.typography.titleMedium,
            fontWeight = FontWeight.SemiBold
        )
        Text(
            text = stringResource(R.string.cheat_manager_category_count, count),
            style = MaterialTheme.typography.labelLarge,
            color = MaterialTheme.colorScheme.onSurfaceVariant
        )
    }
}

@Composable
private fun CheatCategoryBrowserHeader(
    selectedCategory: CheatCategory?,
    count: Int,
    onBack: () -> Unit
) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .padding(horizontal = ScreenHorizontalPadding),
        verticalAlignment = Alignment.CenterVertically
    ) {
        if (selectedCategory == null) {
            Icon(
                imageVector = Icons.Rounded.Folder,
                contentDescription = null,
                tint = MaterialTheme.colorScheme.primary
            )
            Spacer(modifier = Modifier.width(9.dp))
            Text(
                text = stringResource(R.string.cheat_manager_categories),
                modifier = Modifier.weight(1f),
                style = MaterialTheme.typography.titleMedium,
                fontWeight = FontWeight.SemiBold
            )
        } else {
            TextButton(onClick = onBack, modifier = Modifier.weight(1f)) {
                Icon(Icons.AutoMirrored.Rounded.ArrowBack, contentDescription = null)
                Spacer(modifier = Modifier.width(7.dp))
                Icon(Icons.Rounded.FolderOpen, contentDescription = null)
                Spacer(modifier = Modifier.width(7.dp))
                Text(
                    text = stringResource(selectedCategory.titleRes),
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis
                )
            }
        }
        Text(
            text = stringResource(R.string.cheat_manager_category_count, count),
            style = MaterialTheme.typography.labelLarge,
            color = MaterialTheme.colorScheme.onSurfaceVariant
        )
    }
}

@Composable
private fun CheatCategoryCard(
    category: CheatCategory,
    count: Int,
    onClick: () -> Unit
) {
    Surface(
        onClick = onClick,
        modifier = Modifier
            .fillMaxWidth()
            .padding(horizontal = ScreenHorizontalPadding),
        shape = neonShape(18.dp),
        color = MaterialTheme.colorScheme.surface,
        contentColor = MaterialTheme.colorScheme.onSurface,
        border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.72f))
    ) {
        Row(
            modifier = Modifier.padding(horizontal = 16.dp, vertical = 15.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            Surface(
                shape = neonShape(13.dp),
                color = MaterialTheme.colorScheme.primaryContainer,
                contentColor = MaterialTheme.colorScheme.onPrimaryContainer
            ) {
                Icon(
                    imageVector = Icons.Rounded.Folder,
                    contentDescription = null,
                    modifier = Modifier
                        .padding(10.dp)
                        .size(21.dp)
                )
            }
            Column(modifier = Modifier.weight(1f)) {
                Text(
                    text = stringResource(category.titleRes),
                    style = MaterialTheme.typography.titleMedium,
                    fontWeight = FontWeight.SemiBold
                )
                Text(
                    text = stringResource(R.string.cheat_manager_category_count, count),
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
            }
            Icon(
                imageVector = Icons.AutoMirrored.Rounded.ArrowForward,
                contentDescription = null,
                tint = MaterialTheme.colorScheme.primary
            )
        }
    }
}

@Composable
private fun CheatToggleCard(
    entry: VitaCheatEntry,
    masterEnabled: Boolean,
    onToggle: (Boolean) -> Unit
) {
    Surface(
        onClick = { onToggle(!entry.enabled) },
        enabled = masterEnabled,
        modifier = Modifier
            .fillMaxWidth()
            .padding(horizontal = ScreenHorizontalPadding),
        shape = neonShape(16.dp),
        color = MaterialTheme.colorScheme.surface,
        contentColor = MaterialTheme.colorScheme.onSurface
    ) {
        Row(
            modifier = Modifier.padding(horizontal = 14.dp, vertical = 10.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            Column(modifier = Modifier.weight(1f)) {
                Text(
                    text = entry.name,
                    style = MaterialTheme.typography.bodyLarge
                )
                if (entry.broken) {
                    Text(
                        text = stringResource(R.string.emulation_cheats_broken),
                        style = MaterialTheme.typography.labelSmall,
                        color = MaterialTheme.colorScheme.error
                    )
                }
            }
            Switch(
                checked = entry.enabled,
                enabled = masterEnabled,
                onCheckedChange = null
            )
        }
    }
}

@Composable
private fun CheatSearchEmptyState() {
    Surface(
        modifier = Modifier
            .fillMaxWidth()
            .padding(horizontal = ScreenHorizontalPadding),
        shape = neonShape(18.dp),
        color = MaterialTheme.colorScheme.surface,
        contentColor = MaterialTheme.colorScheme.onSurfaceVariant
    ) {
        Text(
            text = stringResource(R.string.cheat_manager_search_empty),
            modifier = Modifier.padding(horizontal = 16.dp, vertical = 18.dp),
            style = MaterialTheme.typography.bodyLarge
        )
    }
}

@Composable
private fun CheatCatalogCard(
    entry: CheatCatalogEntry,
    installed: Boolean,
    busy: Boolean,
    onDownload: () -> Unit
) {
    Surface(
        modifier = Modifier
            .fillMaxWidth()
            .padding(horizontal = ScreenHorizontalPadding),
        shape = neonShape(18.dp),
        color = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.38f),
        contentColor = MaterialTheme.colorScheme.onSurface
    ) {
        Column(
            modifier = Modifier.padding(14.dp),
            verticalArrangement = Arrangement.spacedBy(8.dp)
        ) {
            Row(verticalAlignment = Alignment.Top) {
                Column(modifier = Modifier.weight(1f)) {
                    Text(
                        text = entry.title.ifBlank { entry.titleId },
                        style = MaterialTheme.typography.titleMedium,
                        fontWeight = FontWeight.SemiBold,
                        maxLines = 2,
                        overflow = TextOverflow.Ellipsis
                    )
                    Text(
                        text = stringResource(
                            R.string.cheat_catalog_meta,
                            entry.blockCount,
                            entry.authors.ifBlank { entry.sourceUrl }
                        ),
                        style = MaterialTheme.typography.labelMedium,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                        maxLines = 1,
                        overflow = TextOverflow.Ellipsis
                    )
                }
                if (installed) {
                    Text(
                        text = stringResource(R.string.cheat_catalog_installed),
                        color = MaterialTheme.colorScheme.primary,
                        style = MaterialTheme.typography.labelLarge
                    )
                }
            }
            if (entry.description.isNotBlank()) {
                Text(
                    text = entry.description,
                    style = MaterialTheme.typography.bodyMedium,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                    maxLines = 3,
                    overflow = TextOverflow.Ellipsis
                )
            }
            Button(
                onClick = onDownload,
                enabled = !busy,
                modifier = Modifier.fillMaxWidth()
            ) {
                if (busy) {
                    CircularProgressIndicator(modifier = Modifier.size(18.dp), strokeWidth = 2.dp)
                } else {
                    Icon(Icons.Rounded.CloudDownload, contentDescription = null, modifier = Modifier.size(18.dp))
                }
                Spacer(modifier = Modifier.width(7.dp))
                Text(
                    text = stringResource(
                        if (installed) R.string.cheat_catalog_reinstall
                        else R.string.cheat_catalog_download
                    ),
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis
                )
            }
        }
    }
}

@Composable
private fun CheatGamePicker(
    games: List<InstalledVitaGame>,
    selectedTitleId: String?,
    selectedGame: InstalledVitaGame?,
    onSelect: (String) -> Unit
) {
    Column(verticalArrangement = Arrangement.spacedBy(10.dp)) {
        Column(modifier = Modifier.padding(horizontal = ScreenHorizontalPadding)) {
            Text(
                text = stringResource(R.string.cheat_manager_select_game),
                style = MaterialTheme.typography.headlineSmall.copy(fontWeight = FontWeight.Bold),
                color = MaterialTheme.colorScheme.onBackground
            )
            Text(
                text = selectedGame?.let { "${it.title} · ${it.titleId}" }.orEmpty(),
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis
            )
        }
        LazyRow(
            horizontalArrangement = Arrangement.spacedBy(12.dp),
            contentPadding = PaddingValues(horizontal = ScreenHorizontalPadding)
        ) {
            items(games, key = { it.titleId }) { game ->
                val selected = game.titleId == selectedTitleId
                Surface(
                    onClick = { onSelect(game.titleId) },
                    shape = neonShape(18.dp),
                    color = if (selected) MaterialTheme.colorScheme.secondaryContainer else MaterialTheme.colorScheme.surfaceContainerHigh,
                    border = BorderStroke(
                        1.dp,
                        if (selected) MaterialTheme.colorScheme.primary.copy(alpha = 0.68f) else MaterialTheme.colorScheme.outlineVariant
                    )
                ) {
                    Row(
                        modifier = Modifier.padding(10.dp),
                        verticalAlignment = Alignment.CenterVertically,
                        horizontalArrangement = Arrangement.spacedBy(10.dp)
                    ) {
                        Surface(
                            modifier = Modifier.size(48.dp),
                            shape = neonShape(12.dp),
                            color = MaterialTheme.colorScheme.surfaceContainerHighest,
                            border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.45f))
                        ) {
                            LocalImage(
                                path = game.iconPath,
                                contentDescription = game.title,
                                fallbackLabel = game.title,
                                modifier = Modifier.fillMaxSize()
                            )
                        }
                        Column(modifier = Modifier.size(width = 178.dp, height = 46.dp)) {
                            Text(
                                text = game.title,
                                style = MaterialTheme.typography.labelLarge.copy(fontWeight = FontWeight.SemiBold),
                                maxLines = 1,
                                overflow = TextOverflow.Ellipsis
                            )
                            Text(
                                text = game.titleId,
                                style = MaterialTheme.typography.labelMedium,
                                color = MaterialTheme.colorScheme.onSurfaceVariant,
                                maxLines = 1,
                                overflow = TextOverflow.Ellipsis
                            )
                        }
                    }
                }
            }
        }
    }
}

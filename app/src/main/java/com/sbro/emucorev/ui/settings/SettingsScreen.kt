package com.sbro.emucorev.ui.settings

import android.net.Uri
import android.widget.Toast
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.annotation.StringRes
import androidx.compose.animation.AnimatedContent
import androidx.compose.animation.SizeTransform
import androidx.compose.animation.core.tween
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.slideInHorizontally
import androidx.compose.animation.slideOutHorizontally
import androidx.compose.animation.togetherWith
import androidx.compose.foundation.ExperimentalFoundationApi
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.background
import androidx.compose.foundation.combinedClickable
import androidx.compose.foundation.gestures.animateScrollBy
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
import androidx.compose.foundation.layout.navigationBarsPadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.statusBarsIgnoringVisibility
import androidx.compose.foundation.lazy.LazyRow
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.rounded.VolumeUp
import androidx.compose.material.icons.rounded.CameraAlt
import androidx.compose.material.icons.rounded.Gamepad
import androidx.compose.material.icons.rounded.GraphicEq
import androidx.compose.material.icons.rounded.Info
import androidx.compose.material.icons.rounded.Memory
import androidx.compose.material.icons.rounded.MoreVert
import androidx.compose.material.icons.rounded.Palette
import androidx.compose.material.icons.rounded.Restore
import androidx.compose.material.icons.rounded.SettingsSuggest
import androidx.compose.material.icons.rounded.Storage
import androidx.compose.material.icons.rounded.Star
import androidx.compose.material.icons.rounded.SystemUpdateAlt
import androidx.compose.material.icons.rounded.Tune
import androidx.compose.material.icons.rounded.Vibration
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.FilterChip
import androidx.compose.material3.FilterChipDefaults
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
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
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.runtime.withFrameNanos
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.lifecycle.viewmodel.compose.viewModel
import com.sbro.emucorev.R
import com.sbro.emucorev.core.VitaCoreConfig
import com.sbro.emucorev.ui.common.NavigationBackButton
import com.sbro.emucorev.ui.common.SettingHelpButton
import com.sbro.emucorev.ui.common.rememberDebouncedClick
import com.sbro.emucorev.ui.theme.ScreenHorizontalPadding

private val SettingsRowHorizontalPadding = ScreenHorizontalPadding
private val SettingsRowInnerHorizontalPadding = 14.dp
private val SettingsRowInnerVerticalPadding = 14.dp

enum class SettingsTab(@param:StringRes val titleRes: Int, val icon: ImageVector) {
    General(R.string.settings_tab_general, Icons.Rounded.Tune),
    Customization(R.string.settings_tab_customization, Icons.Rounded.Palette),
    GameMenu(R.string.settings_game_menu_tab, Icons.Rounded.MoreVert),
    Graphics(R.string.settings_tab_graphics, Icons.Rounded.GraphicEq),
    Audio(R.string.settings_tab_audio, Icons.AutoMirrored.Rounded.VolumeUp),
    Overlay(R.string.settings_tab_overlay, Icons.Rounded.Vibration),
    Controls(R.string.settings_tab_controls, Icons.Rounded.Gamepad),
    Camera(R.string.settings_tab_camera, Icons.Rounded.CameraAlt),
    System(R.string.settings_tab_system, Icons.Rounded.Memory),
    Advanced(R.string.settings_tab_advanced, Icons.Rounded.SettingsSuggest),
    Storage(R.string.settings_tab_storage, Icons.Rounded.Storage),
    Updates(R.string.settings_tab_updates, Icons.Rounded.SystemUpdateAlt),
    Pro(R.string.settings_pro_tab, Icons.Rounded.Star),
    About(R.string.settings_tab_about, Icons.Rounded.Info),
}

@OptIn(ExperimentalLayoutApi::class)
@Composable
fun SettingsScreen(
    initialTab: SettingsTab = SettingsTab.General,
    onBackClick: () -> Unit,
    onOpenLanguageSettings: () -> Unit,
    onOpenVitaLanguageSettings: () -> Unit = {},
    onOpenGpuDriverSettings: () -> Unit = {},
    viewModel: SettingsViewModel = viewModel()
) {
    val context = LocalContext.current
    val uiState by viewModel.uiState.collectAsState()
    val defaults = remember { VitaCoreConfig() }
    var selectedTab by rememberSaveable(initialTab) { mutableStateOf(initialTab) }
    val topInset = WindowInsets.statusBarsIgnoringVisibility.asPaddingValues().calculateTopPadding()
    val backupCreatedMessage = stringResource(R.string.settings_backup_created)
    val backupFailedMessage = stringResource(R.string.settings_backup_failed)
    val restoreCompletedMessage = stringResource(R.string.settings_backup_restored)
    val restoreFailedMessage = stringResource(R.string.settings_backup_restore_failed)

    val backupPicker = rememberLauncherForActivityResult(ActivityResultContracts.CreateDocument("application/json")) { uri: Uri? ->
        uri ?: return@rememberLauncherForActivityResult
        viewModel.exportSettingsBackup(uri) { result ->
            Toast.makeText(
                context,
                if (result.isSuccess) backupCreatedMessage else backupFailedMessage,
                Toast.LENGTH_SHORT
            ).show()
        }
    }
    val restorePicker = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri: Uri? ->
        uri ?: return@rememberLauncherForActivityResult
        viewModel.restoreSettingsBackup(uri) { result ->
            Toast.makeText(
                context,
                if (result.isSuccess) restoreCompletedMessage else restoreFailedMessage,
                Toast.LENGTH_SHORT
            ).show()
        }
    }
    val refreshCoreSettingsClick = rememberDebouncedClick(onClick = viewModel::refreshCoreSettings)
    val createBackupClick = rememberDebouncedClick { backupPicker.launch("emucorev-settings-backup.json") }
    val backClick = rememberDebouncedClick(onClick = onBackClick)
    val resetSettingsClick = rememberDebouncedClick(onClick = viewModel::resetCoreSettingsToDefaults)
    var showResetDialog by rememberSaveable { mutableStateOf(false) }
    var showRestoreBackupDialog by rememberSaveable { mutableStateOf(false) }

    Box(
        modifier = Modifier
            .fillMaxSize()
            .background(MaterialTheme.colorScheme.background)
    ) {
        Column(
            modifier = Modifier
                .fillMaxSize()
                .navigationBarsPadding()
                .verticalScroll(rememberScrollState())
        ) {
            SettingsCompactTopBar(
                title = stringResource(R.string.settings_title),
                subtitle = stringResource(selectedTab.titleRes),
                topInset = topInset,
                onBackClick = backClick,
                onResetSettingsClick = { showResetDialog = true }
            )

            SettingsTabRow(
                selectedTab = selectedTab,
                onSelected = { selectedTab = it }
            )

            Column(
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(top = 12.dp, bottom = 110.dp),
                verticalArrangement = Arrangement.spacedBy(18.dp)
            ) {
                AnimatedContent(
                    targetState = selectedTab,
                    transitionSpec = {
                        val forward = targetState.ordinal > initialState.ordinal
                        val direction = if (forward) 1 else -1
                        (
                            fadeIn(animationSpec = tween(160)) +
                                slideInHorizontally(
                                    animationSpec = tween(240),
                                    initialOffsetX = { width -> (width * 0.08f * direction).toInt() }
                                )
                            ).togetherWith(
                            fadeOut(animationSpec = tween(120)) +
                                slideOutHorizontally(
                                    animationSpec = tween(200),
                                    targetOffsetX = { width -> -(width * 0.05f * direction).toInt() }
                                )
                        ).using(SizeTransform(clip = false))
                    },
                    label = "settings-tab-content"
                ) { targetTab ->
                    Column(
                        modifier = Modifier.fillMaxWidth(),
                        verticalArrangement = Arrangement.spacedBy(18.dp)
                    ) {
                        SettingsTabContent(
                            selectedTab = targetTab,
                            uiState = uiState,
                            defaults = defaults,
                            viewModel = viewModel,
                            onOpenLanguageSettings = onOpenLanguageSettings,
                            onOpenVitaLanguageSettings = onOpenVitaLanguageSettings,
                            onOpenGpuDriverSettings = onOpenGpuDriverSettings,
                            refreshCoreSettingsClick = refreshCoreSettingsClick,
                            createBackupClick = createBackupClick,
                            restoreBackupClick = { showRestoreBackupDialog = true }
                        )
                    }
                }
            }
        }

        if (showResetDialog) {
            AlertDialog(
                onDismissRequest = { showResetDialog = false },
                icon = {
                    Icon(
                        imageVector = Icons.Rounded.Restore,
                        contentDescription = null
                    )
                },
                title = { Text(stringResource(R.string.settings_reset_defaults_title)) },
                text = { Text(stringResource(R.string.settings_reset_defaults_message)) },
                confirmButton = {
                    Button(
                        onClick = {
                            showResetDialog = false
                            resetSettingsClick()
                        }
                    ) {
                        Text(stringResource(R.string.settings_reset_defaults_confirm))
                    }
                },
                dismissButton = {
                    TextButton(onClick = { showResetDialog = false }) {
                        Text(stringResource(R.string.settings_reset_defaults_cancel))
                    }
                }
            )
        }

        if (showRestoreBackupDialog) {
            AlertDialog(
                onDismissRequest = { showRestoreBackupDialog = false },
                icon = {
                    Icon(
                        imageVector = Icons.Rounded.Restore,
                        contentDescription = null
                    )
                },
                title = { Text(stringResource(R.string.settings_backup_restore_title)) },
                text = { Text(stringResource(R.string.settings_backup_restore_message)) },
                confirmButton = {
                    Button(
                        onClick = {
                            showRestoreBackupDialog = false
                            restorePicker.launch(arrayOf("application/json", "text/json", "*/*"))
                        }
                    ) {
                        Text(stringResource(R.string.settings_backup_restore_confirm))
                    }
                },
                dismissButton = {
                    TextButton(onClick = { showRestoreBackupDialog = false }) {
                        Text(stringResource(R.string.settings_updates_cancel))
                    }
                }
            )
        }
    }
}

fun settingsTabFromRoute(value: String?): SettingsTab {
    return SettingsTab.entries.firstOrNull { it.name.equals(value, ignoreCase = true) } ?: SettingsTab.General
}

@Composable
private fun SettingsCompactTopBar(
    title: String,
    subtitle: String,
    topInset: androidx.compose.ui.unit.Dp,
    onBackClick: () -> Unit,
    onResetSettingsClick: () -> Unit
) {
    var menuExpanded by remember { mutableStateOf(false) }

    Surface(
        modifier = Modifier
            .fillMaxWidth()
            .padding(
                start = ScreenHorizontalPadding,
                end = ScreenHorizontalPadding,
                top = topInset,
                bottom = 4.dp
            ),
        shape = RoundedCornerShape(24.dp),
        color = MaterialTheme.colorScheme.surface.copy(alpha = 0.78f),
        tonalElevation = 1.dp,
        shadowElevation = 0.dp,
        border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.62f))
    ) {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .padding(horizontal = 10.dp, vertical = 8.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            NavigationBackButton(
                onClick = onBackClick,
                contentColor = MaterialTheme.colorScheme.onSurface
            )
            Column(
                modifier = Modifier
                    .weight(1f)
                    .padding(start = 14.dp, end = 8.dp)
            ) {
                Text(
                    text = title,
                    style = MaterialTheme.typography.titleLarge.copy(fontWeight = FontWeight.Bold),
                    color = MaterialTheme.colorScheme.onSurface,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis
                )
                Text(
                    text = subtitle,
                    style = MaterialTheme.typography.bodyMedium,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis
                )
            }
            Box {
                SettingsHeaderIconButton(onClick = { menuExpanded = true }) {
                    Icon(
                        imageVector = Icons.Rounded.MoreVert,
                        contentDescription = stringResource(R.string.settings_options_content_description),
                        modifier = Modifier.size(20.dp)
                    )
                }
                DropdownMenu(
                    expanded = menuExpanded,
                    onDismissRequest = { menuExpanded = false }
                ) {
                    DropdownMenuItem(
                        text = { Text(stringResource(R.string.settings_reset_defaults_menu)) },
                        leadingIcon = {
                            Icon(
                                imageVector = Icons.Rounded.Restore,
                                contentDescription = null
                            )
                        },
                        onClick = {
                            menuExpanded = false
                            onResetSettingsClick()
                        }
                    )
                }
            }
        }
    }
}

@Composable
private fun SettingsHeaderIconButton(
    onClick: () -> Unit,
    content: @Composable () -> Unit
) {
    Surface(
        modifier = Modifier.size(40.dp),
        shape = RoundedCornerShape(14.dp),
        color = MaterialTheme.colorScheme.surface,
        contentColor = MaterialTheme.colorScheme.onSurface,
        tonalElevation = 3.dp,
        shadowElevation = 5.dp,
        border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.55f)),
        onClick = rememberDebouncedClick(onClick = onClick)
    ) {
        Box(
            modifier = Modifier.fillMaxSize(),
            contentAlignment = Alignment.Center
        ) {
            content()
        }
    }
}

@Composable
private fun SettingsTabRow(
    selectedTab: SettingsTab,
    onSelected: (SettingsTab) -> Unit
) {
    val tabs = remember { SettingsTab.entries.toList() }
    val listState = rememberLazyListState()
    LaunchedEffect(selectedTab) {
        val selectedIndex = tabs.indexOf(selectedTab).coerceAtLeast(0)
        var selectedItem = listState.layoutInfo.visibleItemsInfo
            .firstOrNull { it.index == selectedIndex }
        if (selectedItem == null) {
            listState.scrollToItem(selectedIndex)
            withFrameNanos { }
            selectedItem = listState.layoutInfo.visibleItemsInfo
                .firstOrNull { it.index == selectedIndex }
        }

        selectedItem?.let { item ->
            val layoutInfo = listState.layoutInfo
            val delta = centeredTabScrollDelta(
                itemOffset = item.offset,
                itemSize = item.size,
                viewportStart = layoutInfo.viewportStartOffset,
                viewportEnd = layoutInfo.viewportEndOffset
            )
            if (kotlin.math.abs(delta) > 1f) {
                listState.animateScrollBy(delta)
            }
        }
    }
    LazyRow(
        state = listState,
        modifier = Modifier
            .fillMaxWidth()
            .padding(vertical = 6.dp),
        contentPadding = PaddingValues(horizontal = ScreenHorizontalPadding),
        horizontalArrangement = Arrangement.spacedBy(8.dp)
    ) {
        items(items = tabs, key = { it.name }) { tab ->
            FilterChip(
                selected = selectedTab == tab,
                onClick = { onSelected(tab) },
                colors = FilterChipDefaults.filterChipColors(
                    containerColor = MaterialTheme.colorScheme.surfaceContainerHigh,
                    labelColor = MaterialTheme.colorScheme.onSurface,
                    iconColor = MaterialTheme.colorScheme.onSurfaceVariant,
                    selectedContainerColor = MaterialTheme.colorScheme.secondaryContainer,
                    selectedLabelColor = MaterialTheme.colorScheme.onSecondaryContainer,
                    selectedLeadingIconColor = MaterialTheme.colorScheme.onSecondaryContainer
                ),
                label = { Text(stringResource(tab.titleRes)) },
                leadingIcon = {
                    Icon(
                        imageVector = tab.icon,
                        contentDescription = null,
                        modifier = Modifier.size(16.dp)
                    )
                }
            )
        }
    }
}





@OptIn(ExperimentalFoundationApi::class)
@Composable
fun SettingToggleRow(
    title: String,
    description: String,
    checked: Boolean,
    onCheckedChange: (Boolean) -> Unit,
    onResetDefault: () -> Unit,
    enabled: Boolean = true
) {
    val context = LocalContext.current
    val resetToastMessage = stringResource(R.string.settings_reset_toast, title)
    val interactionSource = remember { androidx.compose.foundation.interaction.MutableInteractionSource() }

    Surface(
        modifier = Modifier
            .fillMaxWidth()
            .padding(horizontal = SettingsRowHorizontalPadding)
            .combinedClickable(
                interactionSource = interactionSource,
                indication = null,
                onClick = { if (enabled) onCheckedChange(!checked) },
                onLongClick = {
                    if (enabled) {
                        onResetDefault()
                        Toast.makeText(context, resetToastMessage, Toast.LENGTH_SHORT).show()
                    }
                }
            ),
        shape = RoundedCornerShape(18.dp),
        color = MaterialTheme.colorScheme.surfaceContainerHigh,
        tonalElevation = 1.dp,
        shadowElevation = 2.dp,
        border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.62f))
    ) {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .padding(horizontal = SettingsRowInnerHorizontalPadding, vertical = SettingsRowInnerVerticalPadding),
            verticalAlignment = Alignment.CenterVertically
        ) {
            Column(
                modifier = Modifier
                    .weight(1f)
                    .padding(end = 12.dp)
            ) {
                Row(
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.spacedBy(8.dp)
                ) {
                    Text(
                        text = title,
                        style = MaterialTheme.typography.bodyLarge,
                        color = if (enabled) MaterialTheme.colorScheme.onSurface else MaterialTheme.colorScheme.onSurface.copy(alpha = 0.5f),
                        modifier = Modifier.weight(1f, fill = false)
                    )
                    SettingHelpButton(title = title, description = description)
                }
            }
            Switch(
                checked = checked,
                onCheckedChange = onCheckedChange,
                enabled = enabled,
                modifier = Modifier.padding(start = 4.dp)
            )
        }
    }
}

@OptIn(ExperimentalLayoutApi::class, ExperimentalFoundationApi::class)
@Composable
fun SettingChoiceRow(
    title: String,
    description: String,
    onResetDefault: () -> Unit,
    content: @Composable androidx.compose.foundation.layout.FlowRowScope.() -> Unit
) {
    val context = LocalContext.current
    val resetToastMessage = stringResource(R.string.settings_reset_toast, title)
    val interactionSource = remember { androidx.compose.foundation.interaction.MutableInteractionSource() }

    Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
        Row(
            modifier = Modifier
                .padding(horizontal = SettingsRowHorizontalPadding)
                .combinedClickable(
                    interactionSource = interactionSource,
                    indication = null,
                    onClick = {},
                    onLongClick = {
                        onResetDefault()
                        Toast.makeText(context, resetToastMessage, Toast.LENGTH_SHORT).show()
                    }
                ),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(8.dp)
        ) {
            Text(
                text = title,
                style = MaterialTheme.typography.titleMedium.copy(fontWeight = FontWeight.Medium),
                color = MaterialTheme.colorScheme.onSurface,
                modifier = Modifier.weight(1f, fill = false)
            )
            SettingHelpButton(title = title, description = description)
        }
        androidx.compose.foundation.layout.FlowRow(
            modifier = Modifier
                .fillMaxWidth()
                .padding(horizontal = SettingsRowHorizontalPadding),
            horizontalArrangement = Arrangement.spacedBy(8.dp),
            verticalArrangement = Arrangement.spacedBy(8.dp),
            content = content
        )
    }
}

@OptIn(ExperimentalFoundationApi::class)
@Composable
fun SettingSliderRow(
    title: String,
    description: String,
    valueText: String,
    onResetDefault: () -> Unit,
    slider: @Composable () -> Unit
) {
    val context = LocalContext.current
    val resetToastMessage = stringResource(R.string.settings_reset_toast, title)
    val interactionSource = remember { androidx.compose.foundation.interaction.MutableInteractionSource() }

    Column(
        modifier = Modifier
            .fillMaxWidth()
            .padding(horizontal = SettingsRowHorizontalPadding, vertical = 8.dp)
            .combinedClickable(
                interactionSource = interactionSource,
                indication = null,
                onClick = {},
                onLongClick = {
                    onResetDefault()
                    Toast.makeText(context, resetToastMessage, Toast.LENGTH_SHORT).show()
                }
            )
    ) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            Column(modifier = Modifier.weight(1f)) {
                Row(
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.spacedBy(8.dp)
                ) {
                    Text(
                        text = title,
                        style = MaterialTheme.typography.bodyLarge,
                        color = MaterialTheme.colorScheme.onSurface,
                        modifier = Modifier.weight(1f, fill = false)
                    )
                    SettingHelpButton(title = title, description = description)
                }
                Text(
                    text = valueText,
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.primary,
                    fontWeight = FontWeight.Medium
                )
            }
        }
        Box(modifier = Modifier.padding(top = 4.dp)) {
            slider()
        }
    }
}

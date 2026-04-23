package com.sbro.emucorev.ui.settings

import android.net.Uri
import android.widget.Toast
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.annotation.StringRes
import androidx.compose.foundation.ExperimentalFoundationApi
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.background
import androidx.compose.foundation.combinedClickable
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
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.rounded.VolumeUp
import androidx.compose.material.icons.rounded.Gamepad
import androidx.compose.material.icons.rounded.GraphicEq
import androidx.compose.material.icons.rounded.Info
import androidx.compose.material.icons.rounded.Memory
import androidx.compose.material.icons.rounded.SettingsSuggest
import androidx.compose.material.icons.rounded.Storage
import androidx.compose.material.icons.rounded.Tune
import androidx.compose.material.icons.rounded.Vibration
import androidx.compose.material3.FilterChip
import androidx.compose.material3.FilterChipDefaults
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
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

private val SettingsRowHorizontalPadding = 12.dp
private val SettingsRowInnerHorizontalPadding = 14.dp
private val SettingsRowInnerVerticalPadding = 14.dp

enum class SettingsTab(@param:StringRes val titleRes: Int, val icon: ImageVector) {
    General(R.string.settings_tab_general, Icons.Rounded.Tune),
    Graphics(R.string.settings_tab_graphics, Icons.Rounded.GraphicEq),
    Audio(R.string.settings_tab_audio, Icons.AutoMirrored.Rounded.VolumeUp),
    Overlay(R.string.settings_tab_overlay, Icons.Rounded.Vibration),
    Controls(R.string.settings_tab_controls, Icons.Rounded.Gamepad),
    System(R.string.settings_tab_system, Icons.Rounded.Memory),
    Advanced(R.string.settings_tab_advanced, Icons.Rounded.SettingsSuggest),
    Storage(R.string.settings_tab_storage, Icons.Rounded.Storage),
    About(R.string.settings_tab_about, Icons.Rounded.Info),
}

@OptIn(ExperimentalLayoutApi::class)
@Composable
fun SettingsScreen(
    initialTab: SettingsTab = SettingsTab.General,
    onBackClick: () -> Unit,
    viewModel: SettingsViewModel = viewModel()
) {
    val context = LocalContext.current
    val uiState by viewModel.uiState.collectAsState()
    val defaults = remember { VitaCoreConfig() }
    var selectedTab by rememberSaveable(initialTab) { mutableStateOf(initialTab) }
    val topInset = WindowInsets.statusBarsIgnoringVisibility.asPaddingValues().calculateTopPadding() + 8.dp
    val folderPickerFailedMessage = stringResource(R.string.folder_picker_failed)
    val gpuDriverInstallFailedMessage = stringResource(R.string.settings_gpu_driver_install_failed)
    val gpuDriverInstallSuccessTemplate = stringResource(
        R.string.settings_gpu_driver_install_success,
        "%s"
    )

    val folderPicker = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocumentTree()) { uri: Uri? ->
        uri ?: return@rememberLauncherForActivityResult
        runCatching {
            viewModel.onPackagesFolderSelected(uri)
        }.onFailure {
            Toast.makeText(context, folderPickerFailedMessage, Toast.LENGTH_SHORT).show()
        }
    }
    val gpuDriverPicker = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri: Uri? ->
        uri ?: return@rememberLauncherForActivityResult
        viewModel.installGpuDriver(uri) { result ->
            result.onSuccess { driverName ->
                Toast.makeText(
                    context,
                    gpuDriverInstallSuccessTemplate.format(driverName),
                    Toast.LENGTH_SHORT
                ).show()
            }.onFailure {
                Toast.makeText(
                    context,
                    it.message ?: gpuDriverInstallFailedMessage,
                    Toast.LENGTH_LONG
                ).show()
            }
        }
    }
    val refreshCoreSettingsClick = rememberDebouncedClick(onClick = viewModel::refreshCoreSettings)
    val changeFolderClick = rememberDebouncedClick { folderPicker.launch(null) }
    val clearFolderClick = rememberDebouncedClick(onClick = viewModel::clearPackagesFolder)
    val backClick = rememberDebouncedClick(onClick = onBackClick)

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
                onBackClick = backClick
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
                SettingsTabContent(
                    selectedTab = selectedTab,
                    uiState = uiState,
                    defaults = defaults,
                    viewModel = viewModel,
                    refreshCoreSettingsClick = refreshCoreSettingsClick,
                    changeFolderClick = changeFolderClick,
                    clearFolderClick = clearFolderClick,
                    installGpuDriverClick = { gpuDriverPicker.launch(arrayOf("application/zip", "*/*")) }
                )
            }
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
    onBackClick: () -> Unit
) {
    Surface(
        modifier = Modifier
            .fillMaxWidth()
            .padding(
                start = ScreenHorizontalPadding,
                end = ScreenHorizontalPadding,
                top = topInset + 8.dp,
                bottom = 4.dp
            ),
        shape = RoundedCornerShape(26.dp),
        color = MaterialTheme.colorScheme.surface,
        tonalElevation = 4.dp,
        shadowElevation = 8.dp,
        border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.7f))
    ) {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .padding(horizontal = 10.dp, vertical = 10.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            NavigationBackButton(
                onClick = onBackClick,
                contentColor = MaterialTheme.colorScheme.onSurface
            )
            Column(
                modifier = Modifier
                    .weight(1f)
                    .padding(start = 4.dp, end = 12.dp)
            ) {
                Text(
                    text = title,
                    style = MaterialTheme.typography.headlineSmall.copy(fontWeight = FontWeight.Bold),
                    color = MaterialTheme.colorScheme.onSurface,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis
                )
                Text(
                    text = subtitle,
                    style = MaterialTheme.typography.bodyMedium,
                    color = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.82f),
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis
                )
            }
        }
    }
}

@Composable
private fun SettingsTabRow(
    selectedTab: SettingsTab,
    onSelected: (SettingsTab) -> Unit
) {
    val tabs = remember { SettingsTab.entries.toList() }
    LazyRow(
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
    onResetDefault: () -> Unit
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
                onClick = { onCheckedChange(!checked) },
                onLongClick = {
                    onResetDefault()
                    Toast.makeText(context, resetToastMessage, Toast.LENGTH_SHORT).show()
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
                        color = MaterialTheme.colorScheme.onSurface,
                        modifier = Modifier.weight(1f, fill = false)
                    )
                    SettingHelpButton(title = title, description = description)
                }
            }
            Switch(
                checked = checked,
                onCheckedChange = onCheckedChange,
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

@OptIn(ExperimentalFoundationApi::class)
@Composable
fun SettingHeader(
    title: String,
    description: String,
    modifier: Modifier = Modifier,
    onResetDefault: (() -> Unit)? = null
) {
    val context = LocalContext.current
    val resetToastMessage = stringResource(R.string.settings_reset_toast, title)
    val resetClick = onResetDefault?.let {
        rememberDebouncedClick {
            it()
            Toast.makeText(
                context,
                resetToastMessage,
                Toast.LENGTH_SHORT
            ).show()
        }
    }
    val headerModifier = if (resetClick != null) {
        modifier.combinedClickable(
            onClick = {},
            onLongClick = resetClick
        )
    } else {
        modifier
    }

    Row(
        modifier = headerModifier,
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(8.dp)
    ) {
        Text(
            text = title,
            modifier = Modifier.weight(1f, fill = false),
            color = MaterialTheme.colorScheme.onSurface,
            style = MaterialTheme.typography.titleMedium.copy(fontWeight = FontWeight.Medium)
        )
        SettingHelpButton(title = title, description = description)
    }
}

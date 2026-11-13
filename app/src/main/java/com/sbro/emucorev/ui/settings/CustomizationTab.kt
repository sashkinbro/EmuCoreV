package com.sbro.emucorev.ui.settings

import androidx.compose.runtime.setValue
import androidx.compose.runtime.getValue
import androidx.compose.foundation.interaction.MutableInteractionSource
import com.sbro.emucorev.ui.common.gamepadFocusableCard
import com.sbro.emucorev.ui.theme.neon.neonChipShape
import android.net.Uri
import android.widget.Toast
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import com.sbro.emucorev.ui.theme.neon.neonShape

import com.sbro.emucorev.ui.theme.neon.neonPillShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.Folder
import androidx.compose.material.icons.rounded.DeleteOutline
import androidx.compose.material.icons.rounded.CheckCircle
import androidx.compose.material.icons.rounded.Image
import androidx.compose.material.icons.rounded.Restore
import androidx.compose.material.icons.rounded.TextFields
import com.sbro.emucorev.ui.common.AppAlertDialog as AlertDialog
import androidx.compose.material3.FilterChip
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.Slider
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.draw.clip
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalWindowInfo
import androidx.compose.ui.res.pluralStringResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.stateDescription
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import com.sbro.emucorev.R
import com.sbro.emucorev.data.AppFont
import com.sbro.emucorev.data.AppPreferences
import com.sbro.emucorev.data.CoverArtRepository
import com.sbro.emucorev.data.VitaCoverStyle
import com.sbro.emucorev.data.CustomizationSettings
import com.sbro.emucorev.data.TouchControlVisualStyle
import com.sbro.emucorev.ui.common.SectionCard
import com.sbro.emucorev.ui.common.GameCoverAspectRatio
import com.sbro.emucorev.ui.common.horizontalBleed
import com.sbro.emucorev.ui.common.rememberDebouncedClick
import com.sbro.emucorev.ui.theme.ThemeMode
import com.sbro.emucorev.ui.theme.neon.LocalNeonTheme
import com.sbro.emucorev.ui.theme.neon.NeonBlack
import com.sbro.emucorev.ui.theme.neon.NeonBlue
import com.sbro.emucorev.ui.theme.neon.neonAccentColor
import com.sbro.emucorev.ui.theme.neon.neonCornerAccent
import com.sbro.emucorev.ui.library.LibraryGridSizing
import java.io.File

@Composable
fun CustomizationTab(
    settings: CustomizationSettings,
    viewModel: SettingsViewModel,
    libraryContentWidthDp: Float
) {
    val context = LocalContext.current
    val preferences = remember(context) { AppPreferences(context) }
    val coverArt = remember(context) { CoverArtRepository.get(context) }
    val coverStyle by coverArt.style.collectAsState()
    val clearingCoverCache by viewModel.coverCacheClearInProgress.collectAsState()
    val themeMode by preferences.themeModeFlow.collectAsState(initial = preferences.themeMode)
    val backgroundImported = stringResource(R.string.customization_background_imported)
    val backgroundFailed = stringResource(R.string.customization_background_failed)
    val fontImported = stringResource(R.string.customization_font_imported)
    val fontFailed = stringResource(R.string.customization_font_failed)
    var showResetDialog by rememberSaveable { mutableStateOf(false) }
    var showClearCoverCacheDialog by rememberSaveable { mutableStateOf(false) }
    val coverCacheClearedMessage = stringResource(R.string.settings_clear_cover_cache_success)
    val coverCachePartiallyClearedMessage = stringResource(R.string.settings_clear_cover_cache_partial)

    val backgroundPicker = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri: Uri? ->
        uri ?: return@rememberLauncherForActivityResult
        viewModel.importCustomizationBackground(uri) { result ->
            Toast.makeText(
                context,
                if (result.isSuccess) backgroundImported else backgroundFailed,
                Toast.LENGTH_SHORT
            ).show()
        }
    }
    val fontPicker = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri: Uri? ->
        uri ?: return@rememberLauncherForActivityResult
        viewModel.importCustomFont(uri) { result ->
            Toast.makeText(
                context,
                if (result.isSuccess) fontImported else fontFailed,
                Toast.LENGTH_SHORT
            ).show()
        }
    }

    Column(verticalArrangement = Arrangement.spacedBy(18.dp)) {
        CustomizationPreview(settings, libraryContentWidthDp)

        SectionCard(
            title = stringResource(R.string.customization_library_layout),
            contentPadding = androidx.compose.foundation.layout.PaddingValues(14.dp)
        ) {
            SettingChoiceRow(
                title = stringResource(R.string.settings_cover_art_style),
                description = stringResource(R.string.settings_help_cover_art_style),
                onResetDefault = { coverArt.setStyle(VitaCoverStyle.THREE_D) }
            ) {
                VitaCoverStyle.entries.forEach { style ->
                    FilterChip(
                        selected = coverStyle == style,
                        onClick = { coverArt.setStyle(style) },
                        shape = com.sbro.emucorev.ui.theme.neon.neonChipShape(),
                        colors = com.sbro.emucorev.ui.common.premiumFilterChipColors(),
                        label = {
                            Text(stringResource(if (style == VitaCoverStyle.THREE_D)
                                R.string.settings_cover_art_style_3d else R.string.settings_cover_art_style_flat))
                        }
                    )
                }
            }
            CustomizationSlider(
                title = stringResource(R.string.customization_cover_size),
                valueLabel = stringResource(
                    R.string.customization_percent_value,
                    settings.coverSizePercent
                ),
                value = settings.coverSizePercent,
                range = CustomizationSettings.MIN_COVER_SIZE_PERCENT..
                    CustomizationSettings.MAX_COVER_SIZE_PERCENT,
                onValueChange = viewModel::updateCoverSizePercent
            )
            CoverCacheActionRow(
                busy = clearingCoverCache,
                onClick = { showClearCoverCacheDialog = true }
            )
        }

        SectionCard(
            title = stringResource(R.string.settings_theme),
            contentPadding = androidx.compose.foundation.layout.PaddingValues(14.dp)
        ) {
            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .horizontalBleed(14.dp)
                    .horizontalScroll(rememberScrollState())
                    .padding(horizontal = 14.dp),
                horizontalArrangement = Arrangement.spacedBy(10.dp)
            ) {
                ThemeChip(
                    selected = themeMode == ThemeMode.SYSTEM,
                    label = stringResource(R.string.settings_theme_system),
                    onClick = { preferences.themeMode = ThemeMode.SYSTEM }
                )
                ThemeChip(
                    selected = themeMode == ThemeMode.LIGHT,
                    label = stringResource(R.string.settings_theme_light),
                    onClick = { preferences.themeMode = ThemeMode.LIGHT }
                )
                ThemeChip(
                    selected = themeMode == ThemeMode.DARK,
                    label = stringResource(R.string.settings_theme_dark),
                    onClick = { preferences.themeMode = ThemeMode.DARK }
                )
                ThemeChip(
                    selected = themeMode == ThemeMode.NEON,
                    label = stringResource(R.string.settings_theme_neon),
                    onClick = {
                        if (themeMode != ThemeMode.NEON) {
                            viewModel.updateTouchControlVisualStyle(TouchControlVisualStyle.MODERN)
                            preferences.themeMode = ThemeMode.NEON
                        }
                    }
                )
                ThemeChip(
                    selected = themeMode == ThemeMode.PRO,
                    label = stringResource(
                        if (preferences.proUnlocked) R.string.settings_theme_pro
                        else R.string.settings_theme_pro_locked
                    ),
                    enabled = preferences.proUnlocked,
                    onClick = { preferences.themeMode = ThemeMode.PRO }
                )
            }
        }

        SectionCard(
            title = stringResource(R.string.customization_home_background),
            contentPadding = androidx.compose.foundation.layout.PaddingValues(14.dp)
        ) {
            CustomizationActionRow(
                icon = if (settings.backgroundPath == null) {
                    Icons.Rounded.Image
                } else {
                    Icons.Rounded.CheckCircle
                },
                title = stringResource(
                    if (settings.backgroundPath == null) {
                        R.string.customization_background_select
                    } else {
                        R.string.customization_background_selected
                    }
                ),
                subtitle = settings.backgroundPath?.let { File(it).name }
                    ?: stringResource(R.string.customization_background_default),
                selected = settings.backgroundPath != null,
                onClick = { backgroundPicker.launch(arrayOf("image/*", "video/*")) }
            )
        }

        TouchControlStyleSection(
            settings = settings,
            onStyleSelected = viewModel::updateTouchControlVisualStyle,
            onPressEffectSelected = viewModel::updateTouchControlPressEffect
        )

        DrawerStyleSection(
            selected = settings.drawerVisualStyle,
            onSelected = viewModel::updateDrawerVisualStyle
        )

        GameMenuStyleSection(
            selected = settings.gameMenuLayoutStyle,
            onSelected = viewModel::updateGameMenuLayoutStyle
        )

        SectionCard(
            title = stringResource(R.string.customization_text_and_fonts),
            contentPadding = androidx.compose.foundation.layout.PaddingValues(14.dp)
        ) {
            Text(
                text = stringResource(R.string.customization_app_font),
                style = MaterialTheme.typography.titleMedium.copy(fontWeight = FontWeight.SemiBold)
            )
            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .horizontalBleed(14.dp)
                    .horizontalScroll(rememberScrollState())
                    .padding(horizontal = 14.dp),
                horizontalArrangement = Arrangement.spacedBy(10.dp)
            ) {
                FontChip(
                    selected = settings.appFont == AppFont.SYSTEM,
                    label = stringResource(R.string.customization_font_system),
                    onClick = { viewModel.updateAppFont(AppFont.SYSTEM) }
                )
                FontChip(
                    selected = settings.appFont == AppFont.RUBIK,
                    label = "Rubik",
                    onClick = { viewModel.updateAppFont(AppFont.RUBIK) }
                )
                FontChip(
                    selected = settings.appFont == AppFont.EXO2,
                    label = "Exo 2",
                    onClick = { viewModel.updateAppFont(AppFont.EXO2) }
                )
                if (settings.customFontPath != null) {
                    FontChip(
                        selected = settings.appFont == AppFont.CUSTOM,
                        label = stringResource(R.string.customization_font_custom),
                        onClick = { viewModel.updateAppFont(AppFont.CUSTOM) }
                    )
                }
            }
            CustomizationActionRow(
                icon = Icons.Rounded.Folder,
                title = stringResource(R.string.customization_import_font),
                subtitle = stringResource(R.string.customization_import_font_hint),
                onClick = { fontPicker.launch(arrayOf("font/ttf", "font/otf", "application/x-font-ttf", "application/x-font-opentype")) }
            )
            CustomizationSlider(
                title = stringResource(R.string.customization_text_size),
                valueLabel = stringResource(
                    R.string.customization_percent_value,
                    settings.textSizePercent
                ),
                value = settings.textSizePercent,
                range = CustomizationSettings.MIN_TEXT_SIZE_PERCENT..
                    CustomizationSettings.MAX_TEXT_SIZE_PERCENT,
                onValueChange = viewModel::updateTextSizePercent
            )
        }

        SectionCard(
            title = stringResource(R.string.customization_reset_section),
            contentPadding = androidx.compose.foundation.layout.PaddingValues(14.dp)
        ) {
            CustomizationActionRow(
                icon = Icons.Rounded.Restore,
                title = stringResource(R.string.customization_reset),
                subtitle = stringResource(R.string.customization_reset_description),
                onClick = { showResetDialog = true }
            )
        }
    }

    if (showClearCoverCacheDialog) {
        AlertDialog(
            onDismissRequest = { showClearCoverCacheDialog = false },
            icon = {
                Icon(
                    imageVector = Icons.Rounded.DeleteOutline,
                    contentDescription = null,
                    tint = MaterialTheme.colorScheme.primary
                )
            },
            title = { Text(stringResource(R.string.settings_clear_cover_cache_confirm_title)) },
            text = { Text(stringResource(R.string.settings_clear_cover_cache_confirm_body)) },
            confirmButton = {
                TextButton(
                    enabled = !clearingCoverCache,
                    onClick = {
                        showClearCoverCacheDialog = false
                        viewModel.clearCoverCache { result ->
                            Toast.makeText(
                                context,
                                if (result.fullyCleared) coverCacheClearedMessage else coverCachePartiallyClearedMessage,
                                Toast.LENGTH_SHORT
                            ).show()
                        }
                    }
                ) {
                    Text(stringResource(R.string.settings_clear_cover_cache_action))
                }
            },
            dismissButton = {
                TextButton(onClick = { showClearCoverCacheDialog = false }) {
                    Text(stringResource(android.R.string.cancel))
                }
            }
        )
    }

    if (showResetDialog) {
        AlertDialog(
            onDismissRequest = { showResetDialog = false },
            title = { Text(stringResource(R.string.customization_reset_confirm_title)) },
            text = { Text(stringResource(R.string.customization_reset_confirm_body)) },
            confirmButton = {
                TextButton(
                    onClick = {
                        viewModel.resetCustomization()
                        coverArt.setStyle(VitaCoverStyle.THREE_D)
                        showResetDialog = false
                    }
                ) {
                    Text(stringResource(R.string.customization_reset_action))
                }
            },
            dismissButton = {
                TextButton(onClick = { showResetDialog = false }) {
                    Text(stringResource(R.string.common_cancel))
                }
            }
        )
    }
}

/** X SettingsItem cover action, hosted in V's existing Customization section. */
@Composable
private fun CoverCacheActionRow(busy: Boolean, onClick: () -> Unit) {
    val label = stringResource(R.string.settings_clear_cover_cache)
    val busyLabel = stringResource(R.string.common_loading)
    val enabled = !busy
    val debouncedClick = rememberDebouncedClick(onClick = onClick)
    val interactionSource = remember { MutableInteractionSource() }
    val shape = neonShape(18.dp)
    Surface(
        modifier = Modifier
            .fillMaxWidth()
            .neonCornerAccent(accent = neonAccentColor(label.hashCode().mod(3)), markSize = 9.dp)
            .gamepadFocusableCard(
                enabled = enabled,
                shape = shape,
                interactionSource = interactionSource,
                addFocusTarget = false
            )
            .then(if (busy) Modifier.semantics { stateDescription = busyLabel } else Modifier),
        shape = shape,
        color = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.3f),
        interactionSource = interactionSource,
        enabled = enabled,
        onClick = debouncedClick
    ) {
        Column(
            modifier = Modifier.padding(horizontal = 14.dp, vertical = 14.dp),
            verticalArrangement = Arrangement.spacedBy(10.dp)
        ) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Box(
                    modifier = Modifier
                        .size(38.dp)
                        .clip(neonShape(12.dp))
                        .then(
                            if (LocalNeonTheme.current) {
                                Modifier
                                    .background(if (enabled) NeonBlack else NeonBlack.copy(alpha = 0.6f))
                                    .border(1.dp, NeonBlue.copy(alpha = if (enabled) 0.35f else 0.18f), neonShape(12.dp))
                            } else {
                                Modifier.background(MaterialTheme.colorScheme.primary.copy(alpha = if (enabled) 0.1f else 0.05f))
                            }
                        ),
                    contentAlignment = Alignment.Center
                ) {
                    Icon(
                        imageVector = Icons.Rounded.DeleteOutline,
                        contentDescription = null,
                        tint = (if (LocalNeonTheme.current) NeonBlue else MaterialTheme.colorScheme.primary)
                            .copy(alpha = if (enabled) 1f else 0.5f),
                        modifier = Modifier.size(20.dp)
                    )
                }
                Spacer(Modifier.width(14.dp))
                Column(modifier = Modifier.weight(1f)) {
                    Text(
                        text = label,
                        style = MaterialTheme.typography.bodyLarge,
                        color = MaterialTheme.colorScheme.onSurface.copy(alpha = if (enabled) 1f else 0.5f)
                    )
                    Text(
                        text = stringResource(R.string.settings_clear_cover_cache_desc),
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = if (enabled) 1f else 0.6f)
                    )
                }
            }
            if (busy) LinearProgressIndicator(modifier = Modifier.fillMaxWidth())
        }
    }
}

@Composable
private fun ThemeChip(
    selected: Boolean,
    label: String,
    enabled: Boolean = true,
    onClick: () -> Unit
) {
    FilterChip(
        colors = com.sbro.emucorev.ui.common.premiumFilterChipColors(),
        shape = neonChipShape(),
        selected = selected,
        onClick = onClick,
        enabled = enabled,
        label = { Text(label) }
    )
}

@Composable
private fun CustomizationPreview(settings: CustomizationSettings, libraryContentWidthDp: Float) {
    val windowInfo = LocalWindowInfo.current
    val containerWidthDp = libraryContentWidthDp
    val isLandscape = windowInfo.containerSize.width > windowInfo.containerSize.height
    val coverScale = settings.coverSizePercent / 100f
    val columns = remember(containerWidthDp, settings.coverSizePercent, isLandscape) {
        LibraryGridSizing.columnsForWidth(
            containerWidthDp,
            settings.coverSizePercent,
            isLandscape = isLandscape
        )
    }
    SectionCard(
        title = stringResource(R.string.customization_preview_title),
        contentPadding = androidx.compose.foundation.layout.PaddingValues(14.dp)
    ) {
        Surface(
            modifier = Modifier
                .fillMaxWidth()
                .height(210.dp),
            shape = neonShape(26.dp),
            color = MaterialTheme.colorScheme.background,
            border = BorderStroke(
                1.dp,
                MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.55f)
            )
        ) {
            Column(
                modifier = Modifier
                    .fillMaxSize()
                    .padding(18.dp),
                verticalArrangement = Arrangement.spacedBy(10.dp)
            ) {
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    Text(
                        text = stringResource(R.string.app_name_emucorev),
                        modifier = Modifier.weight(1f),
                        style = MaterialTheme.typography.titleLarge.copy(fontWeight = FontWeight.Bold),
                        maxLines = 1,
                        overflow = TextOverflow.Ellipsis
                    )
                    Surface(
                        shape = neonPillShape(),
                        color = MaterialTheme.colorScheme.primaryContainer
                    ) {
                        Text(
                            text = pluralStringResource(
                                R.plurals.customization_games_per_row,
                                columns,
                                columns
                            ),
                            modifier = Modifier.padding(horizontal = 10.dp, vertical = 6.dp),
                            style = MaterialTheme.typography.labelLarge,
                            color = MaterialTheme.colorScheme.primary
                        )
                    }
                }
                Text(
                    text = stringResource(R.string.customization_preview_subtitle),
                    modifier = Modifier.fillMaxWidth(),
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
                Row(
                    modifier = Modifier
                        .weight(1f)
                        .fillMaxWidth(),
                    horizontalArrangement = Arrangement.spacedBy(
                        8.dp,
                        Alignment.CenterHorizontally
                    ),
                    verticalAlignment = Alignment.Bottom
                ) {
                    repeat(columns) { index ->
                        val colors = listOf(
                            Color(0xFF17345E),
                            Color(0xFF10233D),
                            Color(0xFF1E1E24),
                            Color(0xFF225C55)
                        )
                        Surface(
                            modifier = Modifier
                                .width(52.dp * coverScale)
                                .aspectRatio(GameCoverAspectRatio),
                            shape = neonShape(12.dp),
                            color = colors[index % colors.size]
                        ) {}
                    }
                }
            }
        }
    }
}

@Composable
private fun CustomizationSlider(
    title: String,
    valueLabel: String,
    value: Int,
    range: IntRange,
    onValueChange: (Int) -> Unit
) {
    Column(verticalArrangement = Arrangement.spacedBy(4.dp)) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            Icon(
                imageVector = Icons.Rounded.TextFields,
                contentDescription = null,
                tint = MaterialTheme.colorScheme.primary
            )
            Spacer(Modifier.width(10.dp))
            Column {
                Text(
                    text = title,
                    style = MaterialTheme.typography.titleMedium.copy(fontWeight = FontWeight.SemiBold)
                )
                Text(valueLabel, color = MaterialTheme.colorScheme.primary)
            }
        }
        Slider(
            value = value.toFloat(),
            onValueChange = { raw ->
                val stepped = ((raw.toInt() + 2) / 5) * 5
                onValueChange(stepped.coerceIn(range))
            },
            valueRange = range.first.toFloat()..range.last.toFloat(),
            steps = ((range.last - range.first) / 5 - 1).coerceAtLeast(0)
        )
    }
}

@Composable
private fun FontChip(
    selected: Boolean,
    label: String,
    onClick: () -> Unit
) {
    FilterChip(
        colors = com.sbro.emucorev.ui.common.premiumFilterChipColors(),
        shape = neonChipShape(),
        selected = selected,
        onClick = onClick,
        label = { Text(label) }
    )
}

@Composable
private fun CustomizationActionRow(
    icon: androidx.compose.ui.graphics.vector.ImageVector,
    title: String,
    subtitle: String,
    selected: Boolean = false,
    onClick: () -> Unit
) {
    val accent = if (selected) Color(0xFF4FC3A1) else MaterialTheme.colorScheme.primary
    val interactionSource = remember { MutableInteractionSource() }
    val shape = neonShape(18.dp)
    Surface(
        modifier = Modifier
            .fillMaxWidth()
            .gamepadFocusableCard(shape = shape, interactionSource = interactionSource, addFocusTarget = false)
            .clickable(interactionSource = interactionSource, indication = null, onClick = onClick),
        shape = shape,
        color = if (selected) {
            accent.copy(alpha = 0.12f)
        } else {
            MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.22f)
        },
        border = if (selected) BorderStroke(2.dp, accent.copy(alpha = 0.72f)) else null
    ) {
        Row(
            modifier = Modifier.padding(horizontal = 14.dp, vertical = 10.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            Surface(
                modifier = Modifier.size(38.dp),
                shape = neonShape(12.dp),
                color = accent.copy(alpha = 0.10f)
            ) {
                Box(contentAlignment = Alignment.Center) {
                    Icon(icon, contentDescription = null, tint = accent, modifier = Modifier.size(20.dp))
                }
            }
            Spacer(Modifier.width(14.dp))
            Column(modifier = Modifier.weight(1f)) {
                Text(
                    text = title,
                    style = MaterialTheme.typography.bodyLarge
                )
                Text(
                    text = subtitle,
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
            }
        }
    }
}

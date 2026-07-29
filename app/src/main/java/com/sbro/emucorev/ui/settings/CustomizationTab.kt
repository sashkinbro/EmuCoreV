package com.sbro.emucorev.ui.settings

import android.net.Uri
import android.widget.Toast
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.Folder
import androidx.compose.material.icons.rounded.Image
import androidx.compose.material.icons.rounded.Restore
import androidx.compose.material.icons.rounded.TextFields
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.FilterChip
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Slider
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.platform.LocalWindowInfo
import androidx.compose.ui.res.pluralStringResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import com.sbro.emucorev.R
import com.sbro.emucorev.data.AppFont
import com.sbro.emucorev.data.CustomizationSettings
import com.sbro.emucorev.ui.common.SectionCard
import com.sbro.emucorev.ui.library.LibraryGridSizing

@Composable
fun CustomizationTab(
    settings: CustomizationSettings,
    viewModel: SettingsViewModel
) {
    val context = LocalContext.current
    val backgroundImported = stringResource(R.string.customization_background_imported)
    val backgroundFailed = stringResource(R.string.customization_background_failed)
    val fontImported = stringResource(R.string.customization_font_imported)
    val fontFailed = stringResource(R.string.customization_font_failed)
    var showResetDialog by rememberSaveable { mutableStateOf(false) }

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
        CustomizationPreview(settings)

        SectionCard(
            title = stringResource(R.string.customization_home_background),
            contentPadding = androidx.compose.foundation.layout.PaddingValues(14.dp)
        ) {
            CustomizationActionRow(
                icon = Icons.Rounded.Image,
                title = stringResource(R.string.customization_background_select),
                subtitle = stringResource(
                    if (settings.backgroundPath == null) {
                        R.string.customization_background_default
                    } else {
                        R.string.customization_background_selected
                    }
                ),
                onClick = { backgroundPicker.launch(arrayOf("image/*", "video/*")) }
            )
        }

        SectionCard(
            title = stringResource(R.string.customization_library_layout),
            contentPadding = androidx.compose.foundation.layout.PaddingValues(14.dp)
        ) {
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
        }

        SectionCard(
            title = stringResource(R.string.customization_text_and_fonts),
            contentPadding = androidx.compose.foundation.layout.PaddingValues(14.dp)
        ) {
            Text(
                text = stringResource(R.string.customization_app_font),
                style = MaterialTheme.typography.titleMedium.copy(fontWeight = FontWeight.SemiBold)
            )
            FlowRow(
                horizontalArrangement = Arrangement.spacedBy(10.dp),
                verticalArrangement = Arrangement.spacedBy(8.dp)
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

    if (showResetDialog) {
        AlertDialog(
            onDismissRequest = { showResetDialog = false },
            title = { Text(stringResource(R.string.customization_reset_confirm_title)) },
            text = { Text(stringResource(R.string.customization_reset_confirm_body)) },
            confirmButton = {
                TextButton(
                    onClick = {
                        viewModel.resetCustomization()
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

@Composable
private fun CustomizationPreview(settings: CustomizationSettings) {
    val density = LocalDensity.current
    val windowInfo = LocalWindowInfo.current
    val containerWidthDp = with(density) { windowInfo.containerSize.width.toDp() }
    val columns = remember(containerWidthDp, settings.coverSizePercent) {
        LibraryGridSizing.columnsForWidth(
            containerWidthDp.value,
            settings.coverSizePercent
        )
    }
    SectionCard(
        title = stringResource(R.string.customization_preview_title),
        contentPadding = androidx.compose.foundation.layout.PaddingValues(14.dp)
    ) {
        Box(modifier = Modifier.fillMaxWidth()) {
            Surface(
                modifier = Modifier.fillMaxWidth(),
                shape = RoundedCornerShape(26.dp),
                color = MaterialTheme.colorScheme.background,
                border = BorderStroke(
                    1.dp,
                    MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.55f)
                )
            ) {
                Column(
                    modifier = Modifier.padding(18.dp),
                    verticalArrangement = Arrangement.spacedBy(18.dp)
                ) {
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        Column(modifier = Modifier.weight(1f)) {
                            Text(
                                text = stringResource(R.string.app_name_emucorev),
                                style = MaterialTheme.typography.titleLarge.copy(fontWeight = FontWeight.Bold)
                            )
                            Text(
                                text = stringResource(R.string.customization_preview_subtitle),
                                maxLines = 1,
                                overflow = TextOverflow.Ellipsis,
                                color = MaterialTheme.colorScheme.onSurfaceVariant
                            )
                        }
                        Surface(
                            shape = RoundedCornerShape(50),
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
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.spacedBy(8.dp),
                        verticalAlignment = Alignment.Bottom
                    ) {
                        repeat(columns) { index ->
                            val colors = listOf(
                                Color(0xFF17345E),
                                Color(0xFF30255C),
                                Color(0xFF704054),
                                Color(0xFF225C55)
                            )
                            Surface(
                                modifier = Modifier
                                    .weight(1f)
                                    .aspectRatio(0.72f),
                                shape = RoundedCornerShape(12.dp),
                                color = colors[index % colors.size]
                            ) {}
                        }
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
    onClick: () -> Unit
) {
    Surface(
        modifier = Modifier
            .fillMaxWidth()
            .clickable(onClick = onClick),
        shape = RoundedCornerShape(22.dp),
        color = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.42f),
        border = BorderStroke(
            1.dp,
            MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.55f)
        )
    ) {
        Row(
            modifier = Modifier.padding(14.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            Surface(
                modifier = Modifier.size(48.dp),
                shape = RoundedCornerShape(16.dp),
                color = MaterialTheme.colorScheme.primary.copy(alpha = 0.12f)
            ) {
                Box(contentAlignment = Alignment.Center) {
                    Icon(icon, contentDescription = null, tint = MaterialTheme.colorScheme.primary)
                }
            }
            Spacer(Modifier.width(14.dp))
            Column(modifier = Modifier.weight(1f)) {
                Text(
                    text = title,
                    style = MaterialTheme.typography.titleMedium.copy(fontWeight = FontWeight.Medium)
                )
                Text(
                    text = subtitle,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
            }
        }
    }
}

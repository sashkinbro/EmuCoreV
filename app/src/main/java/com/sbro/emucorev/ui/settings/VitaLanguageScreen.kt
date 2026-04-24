package com.sbro.emucorev.ui.settings

import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.asPaddingValues
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.navigationBarsPadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.statusBarsIgnoringVisibility
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.lifecycle.viewmodel.compose.viewModel
import com.sbro.emucorev.R
import com.sbro.emucorev.ui.common.NavigationBackButton
import com.sbro.emucorev.ui.common.rememberDebouncedClick
import com.sbro.emucorev.ui.theme.ScreenHorizontalPadding

private data class VitaLangOption(
    val value: Int,
    val shortLabel: String,
    val nativeLabel: String
)

private val VitaLangOptions = listOf(
    VitaLangOption(14, "DA", "Dansk"),
    VitaLangOption(4, "DE", "Deutsch"),
    VitaLangOption(18, "EN", "English (United Kingdom)"),
    VitaLangOption(1, "EN", "English (United States)"),
    VitaLangOption(3, "ES", "Español"),
    VitaLangOption(2, "FR", "Français"),
    VitaLangOption(5, "IT", "Italiano"),
    VitaLangOption(6, "NL", "Nederlands"),
    VitaLangOption(15, "NO", "Norsk"),
    VitaLangOption(16, "PL", "Polski"),
    VitaLangOption(7, "PT", "Português (Portugal)"),
    VitaLangOption(17, "BR", "Português (Brasil)"),
    VitaLangOption(8, "RU", "Русский"),
    VitaLangOption(12, "FI", "Suomi"),
    VitaLangOption(13, "SV", "Svenska"),
    VitaLangOption(19, "TR", "Türkçe"),
    VitaLangOption(0, "JP", "日本語"),
    VitaLangOption(9, "KO", "한국어"),
    VitaLangOption(11, "ZH", "简体中文"),
    VitaLangOption(10, "ZH", "繁體中文")
)

@Composable
@OptIn(ExperimentalLayoutApi::class)
fun VitaLanguageScreen(
    onBackClick: () -> Unit,
    viewModel: SettingsViewModel = viewModel()
) {
    val uiState by viewModel.uiState.collectAsState()
    val topInset = WindowInsets.statusBarsIgnoringVisibility.asPaddingValues().calculateTopPadding() + 16.dp
    val options = remember { VitaLangOptions }
    val backClick = rememberDebouncedClick(onClick = onBackClick)

    LazyColumn(
        modifier = Modifier
            .fillMaxSize()
            .background(MaterialTheme.colorScheme.background)
            .navigationBarsPadding(),
        contentPadding = PaddingValues(
            start = ScreenHorizontalPadding,
            top = topInset,
            end = ScreenHorizontalPadding,
            bottom = 32.dp
        ),
        verticalArrangement = Arrangement.spacedBy(12.dp)
    ) {
        item {
            VitaLanguageHeader(onBackClick = backClick)
        }
        items(options, key = { it.value }) { option ->
            VitaLanguageOptionRow(
                option = option,
                selected = option.value == uiState.coreConfig.sysLang,
                onClick = {
                    viewModel.updateCoreSettings { it.copy(sysLang = option.value) }
                }
            )
        }
    }
}

@Composable
private fun VitaLanguageHeader(onBackClick: () -> Unit) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .padding(bottom = 8.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(14.dp)
    ) {
        NavigationBackButton(
            onClick = onBackClick,
            containerColor = MaterialTheme.colorScheme.surfaceContainerHigh,
            contentColor = MaterialTheme.colorScheme.onSurface
        )
        Text(
            text = stringResource(R.string.settings_vita_language_picker_title),
            style = MaterialTheme.typography.headlineMedium.copy(fontWeight = FontWeight.Bold),
            color = MaterialTheme.colorScheme.onBackground,
            maxLines = 1,
            overflow = TextOverflow.Ellipsis
        )
    }
}

@Composable
private fun VitaLanguageOptionRow(
    option: VitaLangOption,
    selected: Boolean,
    onClick: () -> Unit
) {
    val containerColor = if (selected) {
        MaterialTheme.colorScheme.secondaryContainer
    } else {
        MaterialTheme.colorScheme.surface
    }
    val contentColor = if (selected) {
        MaterialTheme.colorScheme.onSecondaryContainer
    } else {
        MaterialTheme.colorScheme.onSurface
    }
    if (selected) {
        MaterialTheme.colorScheme.onSecondaryContainer.copy(alpha = 0.74f)
    } else {
        MaterialTheme.colorScheme.onSurfaceVariant
    }

    Surface(
        modifier = Modifier.fillMaxWidth(),
        shape = RoundedCornerShape(22.dp),
        color = containerColor,
        tonalElevation = if (selected) 4.dp else 1.dp,
        shadowElevation = if (selected) 5.dp else 2.dp,
        border = BorderStroke(
            1.dp,
            if (selected) {
                MaterialTheme.colorScheme.primary.copy(alpha = 0.30f)
            } else {
                MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.58f)
            }
        ),
        onClick = onClick
    ) {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .padding(horizontal = 16.dp, vertical = 12.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(16.dp)
        ) {
            Box(
                modifier = Modifier
                    .size(44.dp)
                    .clip(RoundedCornerShape(14.dp))
                    .background(
                        if (selected) {
                            MaterialTheme.colorScheme.primary.copy(alpha = 0.16f)
                        } else {
                            MaterialTheme.colorScheme.surfaceContainerHigh
                        }
                    ),
                contentAlignment = Alignment.Center
            ) {
                Text(
                    text = option.shortLabel,
                    style = MaterialTheme.typography.titleMedium.copy(fontWeight = FontWeight.Bold),
                    color = if (selected) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.onSurfaceVariant
                )
            }
            Text(
                text = option.nativeLabel,
                style = MaterialTheme.typography.titleMedium.copy(fontWeight = FontWeight.Bold),
                color = contentColor,
                modifier = Modifier.weight(1f),
                maxLines = 1,
                overflow = TextOverflow.Ellipsis
            )
            if (selected) {
                Spacer(
                    modifier = Modifier
                        .size(12.dp)
                        .background(MaterialTheme.colorScheme.primary, CircleShape)
                )
            }
        }
    }
}

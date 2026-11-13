package com.sbro.emucorev.ui.settings

import androidx.compose.runtime.getValue
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.asPaddingValues
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.navigationBarsPadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.statusBarsIgnoringVisibility
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items

import androidx.compose.material3.MaterialTheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.remember
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import androidx.lifecycle.viewmodel.compose.viewModel
import com.sbro.emucorev.R
import com.sbro.emucorev.ui.common.ScreenTopBar
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
    val topInset = WindowInsets.statusBarsIgnoringVisibility.asPaddingValues().calculateTopPadding()
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
    ScreenTopBar(
        title = stringResource(R.string.settings_vita_language_picker_title),
        onBackClick = onBackClick,
        modifier = Modifier.padding(bottom = 8.dp)
    )
}

@Composable
private fun VitaLanguageOptionRow(
    option: VitaLangOption,
    selected: Boolean,
    onClick: () -> Unit
) {
    SettingsLanguageOptionCard(
        badgeText = option.shortLabel,
        title = option.nativeLabel,
        subtitle = null,
        selected = selected,
        onClick = onClick
    )
}

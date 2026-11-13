package com.sbro.emucorev.ui.settings

import androidx.compose.foundation.BorderStroke
import androidx.compose.runtime.getValue
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.interaction.MutableInteractionSource
import com.sbro.emucorev.ui.common.gamepadFocusableCard
import androidx.annotation.StringRes
import androidx.compose.foundation.background
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
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import com.sbro.emucorev.ui.theme.neon.neonShape

import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.lifecycle.viewmodel.compose.viewModel
import com.sbro.emucorev.R
import com.sbro.emucorev.data.AppLanguage
import com.sbro.emucorev.ui.common.ScreenTopBar
import com.sbro.emucorev.ui.common.rememberDebouncedClick
import com.sbro.emucorev.ui.theme.ScreenHorizontalPadding

private data class AppLanguageOption(
    val language: AppLanguage,
    val shortLabel: String,
    @param:StringRes val titleRes: Int,
    val nativeLabel: String? = null
)

private val AppLanguageOptions = listOf(
    AppLanguageOption(AppLanguage.SYSTEM, "SYS", R.string.settings_app_language_system, ""),
    AppLanguageOption(AppLanguage.ENGLISH, "EN", R.string.settings_app_language_english, "English"),
    AppLanguageOption(AppLanguage.UKRAINIAN, "UA", R.string.settings_app_language_ukrainian, "Українська"),
    AppLanguageOption(AppLanguage.RUSSIAN, "RU", R.string.settings_app_language_russian, "Русский"),
    AppLanguageOption(AppLanguage.SPANISH, "ES", R.string.settings_app_language_spanish, "Español"),
    AppLanguageOption(AppLanguage.FRENCH, "FR", R.string.settings_app_language_french, "Français"),
    AppLanguageOption(AppLanguage.GERMAN, "DE", R.string.settings_app_language_german, "Deutsch"),
    AppLanguageOption(AppLanguage.PORTUGUESE, "PT", R.string.settings_app_language_portuguese, "Português"),
    AppLanguageOption(AppLanguage.CHINESE, "繁", R.string.settings_app_language_chinese_traditional, "繁體中文"),
    AppLanguageOption(AppLanguage.HINDI, "HI", R.string.settings_app_language_hindi, "हिन्दी"),
    AppLanguageOption(AppLanguage.ITALIAN, "IT", R.string.settings_app_language_italian, "Italiano"),
    AppLanguageOption(AppLanguage.TURKISH, "TR", R.string.settings_app_language_turkish, "Türkçe"),
    AppLanguageOption(AppLanguage.ARABIC, "AR", R.string.settings_app_language_arabic, "العربية"),
    AppLanguageOption(AppLanguage.CZECH, "CS", R.string.settings_app_language_czech, "Čeština"),
    AppLanguageOption(AppLanguage.POLISH, "PL", R.string.settings_app_language_polish, "Polski"),
    AppLanguageOption(AppLanguage.JAPANESE, "JA", R.string.settings_app_language_japanese, "日本語"),
    AppLanguageOption(AppLanguage.KOREAN, "KO", R.string.settings_app_language_korean, "한국어"),
    AppLanguageOption(AppLanguage.INDONESIAN, "ID", R.string.settings_app_language_indonesian, "Bahasa Indonesia"),
    AppLanguageOption(AppLanguage.PERSIAN, "FA", R.string.settings_app_language_persian, "فارسی")
)

@Composable
@OptIn(ExperimentalLayoutApi::class)
fun AppLanguageScreen(
    onBackClick: () -> Unit,
    viewModel: SettingsViewModel = viewModel()
) {
    val uiState by viewModel.uiState.collectAsState()
    val topInset = WindowInsets.statusBarsIgnoringVisibility.asPaddingValues().calculateTopPadding()
    val options = remember { AppLanguageOptions }
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
            AppLanguageHeader(onBackClick = backClick)
        }
        items(options, key = { it.language.name }) { option ->
            AppLanguageOptionRow(
                option = option,
                selected = option.language == uiState.appLanguage,
                onClick = {
                    viewModel.updateAppLanguage(option.language)
                }
            )
        }
    }
}

@Composable
private fun AppLanguageHeader(onBackClick: () -> Unit) {
    ScreenTopBar(
        title = stringResource(R.string.settings_language_picker_title),
        onBackClick = onBackClick,
        modifier = Modifier.padding(bottom = 8.dp)
    )
}

@Composable
private fun AppLanguageOptionRow(
    option: AppLanguageOption,
    selected: Boolean,
    onClick: () -> Unit
) {
    SettingsLanguageOptionCard(
        badgeText = option.shortLabel,
        title = stringResource(option.titleRes),
        subtitle = option.nativeLabel?.takeIf { it.isNotEmpty() },
        selected = selected,
        onClick = onClick
    )
}

@Composable
internal fun SettingsLanguageOptionCard(
    badgeText: String,
    title: String,
    subtitle: String?,
    selected: Boolean,
    onClick: () -> Unit
) {
    val interactionSource = remember { MutableInteractionSource() }
    val shape = neonShape(20.dp)
    Surface(
        modifier = Modifier
            .fillMaxWidth()
            .gamepadFocusableCard(
                shape = shape,
                interactionSource = interactionSource,
                addFocusTarget = false
            ),
        shape = shape,
        color = if (selected) {
            MaterialTheme.colorScheme.primary.copy(alpha = 0.12f)
        } else {
            MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.4f)
        },
        interactionSource = interactionSource,
        border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.55f)),
        onClick = onClick
    ) {
        Row(
            modifier = Modifier.padding(horizontal = 14.dp, vertical = 14.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            Box(
                modifier = Modifier
                    .size(42.dp)
                    .clip(neonShape(14.dp))
                    .background(
                        if (selected) MaterialTheme.colorScheme.primary.copy(alpha = 0.18f)
                        else MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.55f)
                    ),
                contentAlignment = Alignment.Center
            ) {
                Text(
                    text = badgeText,
                    style = MaterialTheme.typography.labelLarge.copy(fontWeight = FontWeight.Bold),
                    color = if (selected) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.onSurfaceVariant
                )
            }
            Column(
                modifier = Modifier
                    .weight(1f)
                    .padding(start = 12.dp)
            ) {
                Column(verticalArrangement = Arrangement.spacedBy(2.dp)) {
                    Text(
                        text = title,
                        style = MaterialTheme.typography.bodyLarge.copy(fontWeight = FontWeight.SemiBold),
                        color = if (selected) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.onSurface
                    )
                    subtitle?.let {
                        Text(
                            text = it,
                            style = MaterialTheme.typography.bodyMedium,
                            color = MaterialTheme.colorScheme.onSurfaceVariant
                        )
                    }
                }
            }
            if (selected) {
                Box(
                    modifier = Modifier
                        .size(10.dp)
                        .clip(CircleShape)
                        .background(MaterialTheme.colorScheme.primary)
                )
            }
        }
    }
}

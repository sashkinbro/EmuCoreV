package com.sbro.emucorev.ui.settings

import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.FlowRowScope
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.Info
import androidx.compose.material.icons.rounded.Language
import androidx.compose.material.icons.rounded.Memory
import androidx.compose.material3.Button
import androidx.compose.material3.FilterChip
import androidx.compose.material3.FilterChipDefaults
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Slider
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalUriHandler
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import com.sbro.emucorev.R
import com.sbro.emucorev.core.InstalledGpuDriver
import com.sbro.emucorev.core.VitaCoreConfig
import com.sbro.emucorev.data.AppLanguage
import com.sbro.emucorev.ui.common.SectionCard
import com.sbro.emucorev.ui.theme.ScreenHorizontalPadding

private val SettingsSectionContentPadding = 14.dp
private val SettingsSectionRowPadding = 12.dp
private val SettingsCardInnerPadding = 14.dp

private data class VitaSystemLanguageOption(
    val value: Int,
    val nativeLabel: String,
    val shortLabel: String
)

private val VitaSystemLanguages = listOf(
    VitaSystemLanguageOption(14, "Dansk", "DA"),
    VitaSystemLanguageOption(4, "Deutsch", "DE"),
    VitaSystemLanguageOption(18, "English (United Kingdom)", "EN"),
    VitaSystemLanguageOption(1, "English (United States)", "EN"),
    VitaSystemLanguageOption(3, "Espanol", "ES"),
    VitaSystemLanguageOption(2, "Francais", "FR"),
    VitaSystemLanguageOption(5, "Italiano", "IT"),
    VitaSystemLanguageOption(6, "Nederlands", "NL"),
    VitaSystemLanguageOption(15, "Norsk", "NO"),
    VitaSystemLanguageOption(16, "Polski", "PL"),
    VitaSystemLanguageOption(7, "Portugues (Portugal)", "PT"),
    VitaSystemLanguageOption(17, "Portugues (Brasil)", "BR"),
    VitaSystemLanguageOption(8, "Русский", "RU"),
    VitaSystemLanguageOption(12, "Suomi", "FI"),
    VitaSystemLanguageOption(13, "Svenska", "SV"),
    VitaSystemLanguageOption(19, "Turkce", "TR"),
    VitaSystemLanguageOption(0, "日本語", "JP"),
    VitaSystemLanguageOption(9, "Korean", "KO"),
    VitaSystemLanguageOption(11, "简体中文", "ZH"),
    VitaSystemLanguageOption(10, "繁體中文", "ZH")
).distinctBy { it.value }


@Composable
fun SettingsTabContent(
    selectedTab: SettingsTab,
    uiState: SettingsUiState,
    defaults: VitaCoreConfig,
    viewModel: SettingsViewModel,
    onOpenLanguageSettings: () -> Unit,
    onOpenVitaLanguageSettings: () -> Unit = {},
    refreshCoreSettingsClick: () -> Unit,
    changeFolderClick: () -> Unit,
    clearFolderClick: () -> Unit,
    installGpuDriverClick: () -> Unit
) {
    when (selectedTab) {
        SettingsTab.General -> GeneralTab(uiState, defaults, viewModel, onOpenLanguageSettings, onOpenVitaLanguageSettings)
        SettingsTab.Graphics -> GraphicsTab(uiState, defaults, viewModel, installGpuDriverClick)
        SettingsTab.Audio -> AudioTab(uiState, defaults, viewModel, refreshCoreSettingsClick)
        SettingsTab.Overlay -> OverlayTab(uiState, defaults, viewModel)
        SettingsTab.Controls -> ControlsTab(uiState, defaults, viewModel)
        SettingsTab.System -> SystemTab(uiState, defaults, viewModel)
        SettingsTab.Advanced -> AdvancedTab(uiState, defaults, viewModel)
        SettingsTab.Storage -> StorageTab(uiState, changeFolderClick, clearFolderClick)
        SettingsTab.About -> AboutTab()
    }
}

@Composable
private fun GeneralTab(
    uiState: SettingsUiState,
    defaults: VitaCoreConfig,
    viewModel: SettingsViewModel,
    onOpenLanguageSettings: () -> Unit,
    onOpenVitaLanguageSettings: () -> Unit = {}
) {
    SectionCard(title = stringResource(R.string.settings_tab_general), contentPadding = androidx.compose.foundation.layout.PaddingValues(SettingsSectionContentPadding)) {
        AppLanguageSettingRow(
            selectedLanguage = uiState.appLanguage,
            onClick = onOpenLanguageSettings
        )
        VitaLanguageSettingRow(
            selectedLanguage = uiState.coreConfig.sysLang,
            onClick = onOpenVitaLanguageSettings
        )
        Toggle(title = stringResource(R.string.settings_core_pstv_mode), description = stringResource(R.string.settings_help_pstv_mode), checked = uiState.coreConfig.pstvMode, onCheckedChange = { enabled -> viewModel.updateCoreSettings { it.copy(pstvMode = enabled) } }, onResetDefault = { viewModel.updateCoreSettings { it.copy(pstvMode = defaults.pstvMode) } })
        Toggle(title = stringResource(R.string.settings_show_info_bar), description = stringResource(R.string.settings_help_show_info_bar), checked = uiState.coreConfig.showInfoBar, onCheckedChange = { enabled -> viewModel.updateCoreSettings { it.copy(showInfoBar = enabled) } }, onResetDefault = { viewModel.updateCoreSettings { it.copy(showInfoBar = defaults.showInfoBar) } })
        Chips(title = stringResource(R.string.settings_confirm_button), description = stringResource(R.string.settings_help_confirm_button), onResetDefault = { viewModel.updateCoreSettings { it.copy(sysButton = defaults.sysButton) } }) {
            ButtonChip(1, stringResource(R.string.settings_confirm_cross), uiState, viewModel)
            ButtonChip(0, stringResource(R.string.settings_confirm_circle), uiState, viewModel)
        }
        Chips(title = stringResource(R.string.settings_modules_mode), description = stringResource(R.string.settings_help_modules_mode), onResetDefault = { viewModel.updateCoreSettings { it.copy(modulesMode = defaults.modulesMode) } }) {
            ModeChip(
                0,
                stringResource(R.string.settings_modules_mode_automatic),
                uiState,
                viewModel
            )
            ModeChip(
                1,
                stringResource(R.string.settings_modules_mode_auto_manual),
                uiState,
                viewModel
            )
            ModeChip(2, stringResource(R.string.settings_modules_mode_manual), uiState, viewModel)
        }
        SliderRow(title = stringResource(R.string.settings_cpu_pool_size), description = stringResource(R.string.settings_help_cpu_pool_size), valueText = stringResource(R.string.settings_cpu_pool_size_value, uiState.coreConfig.cpuPoolSize), onResetDefault = { viewModel.updateCoreSettings { it.copy(cpuPoolSize = defaults.cpuPoolSize) } }) {
            Slider(value = uiState.coreConfig.cpuPoolSize.toFloat(), onValueChange = { value -> viewModel.updateCoreSettings { it.copy(cpuPoolSize = value.toInt().coerceIn(1, 32)) } }, valueRange = 1f..32f, steps = 30)
        }
    }

}

@Composable
private fun GraphicsTab(uiState: SettingsUiState, defaults: VitaCoreConfig, viewModel: SettingsViewModel, installGpuDriverClick: () -> Unit) {
    val selectedDriver = uiState.installedGpuDrivers.firstOrNull { it.name == uiState.coreConfig.customDriverName }
    val customDriverAvailable = uiState.coreConfig.backendRenderer == "Vulkan" && selectedDriver?.isUsable == true

    SectionCard(title = stringResource(R.string.settings_tab_graphics), contentPadding = androidx.compose.foundation.layout.PaddingValues(SettingsSectionContentPadding)) {
        Chips(title = stringResource(R.string.settings_core_renderer_label), description = stringResource(R.string.settings_help_renderer), onResetDefault = { viewModel.updateCoreSettings { it.copy(backendRenderer = defaults.backendRenderer) } }) {
            TextChip(
                stringResource(R.string.settings_renderer_vulkan),
                uiState.coreConfig.backendRenderer,
                viewModel
            ) { config, value -> config.copy(backendRenderer = value) }
            TextChip(
                stringResource(R.string.settings_renderer_opengl),
                uiState.coreConfig.backendRenderer,
                viewModel
            ) { config, value -> config.copy(backendRenderer = value) }
        }
        Chips(
            title = stringResource(R.string.settings_gpu_driver),
            description = stringResource(R.string.settings_help_gpu_driver),
            onResetDefault = { viewModel.updateCoreSettings { it.copy(customDriverName = defaults.customDriverName) } }
        ) {
            FilterChip(
                selected = uiState.coreConfig.customDriverName.isBlank(),
                onClick = { viewModel.updateCoreSettings { it.copy(customDriverName = "") } },
                label = { Text(stringResource(R.string.settings_gpu_driver_system)) }
            )
            FilterChip(
                selected = uiState.coreConfig.customDriverName.isNotBlank(),
                onClick = {
                    val firstDriver = uiState.installedGpuDrivers.firstOrNull()
                    if (firstDriver != null) {
                        viewModel.updateCoreSettings { it.copy(customDriverName = firstDriver.name) }
                    }
                },
                enabled = uiState.installedGpuDrivers.isNotEmpty(),
                label = { Text(stringResource(R.string.settings_gpu_driver_custom)) }
            )
        }
        GpuDriverStatus(
            selectedDriver = selectedDriver,
            backendRenderer = uiState.coreConfig.backendRenderer,
            isActive = customDriverAvailable,
            modifier = Modifier.padding(horizontal = SettingsSectionRowPadding)
        )
        if (uiState.installedGpuDrivers.isNotEmpty()) {
            Chips(
                title = stringResource(R.string.settings_gpu_driver_installed),
                description = stringResource(R.string.settings_help_gpu_driver_installed),
                onResetDefault = { viewModel.updateCoreSettings { it.copy(customDriverName = "") } }
            ) {
                uiState.installedGpuDrivers.forEach { driver ->
                    FilterChip(
                        selected = uiState.coreConfig.customDriverName == driver.name,
                        onClick = { viewModel.updateCoreSettings { it.copy(customDriverName = driver.name) } },
                        colors = appFilterChipColors(),
                        label = { Text(driver.name) }
                    )
                }
            }
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.spacedBy(10.dp)
            ) {
                Button(onClick = installGpuDriverClick, modifier = Modifier.weight(1f)) {
                    Text(stringResource(R.string.settings_gpu_driver_install))
                }
                Button(
                    onClick = { viewModel.removeGpuDriver(uiState.coreConfig.customDriverName) },
                    enabled = uiState.coreConfig.customDriverName.isNotBlank(),
                    modifier = Modifier.weight(1f)
                ) {
                    Text(stringResource(R.string.settings_gpu_driver_remove))
                }
            }
        } else {
            Text(
                text = stringResource(R.string.settings_gpu_driver_none_installed),
                color = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.82f)
            )
            Button(onClick = installGpuDriverClick, modifier = Modifier.padding(top = 8.dp)) {
                Text(stringResource(R.string.settings_gpu_driver_install))
            }
        }
        Toggle(stringResource(R.string.settings_core_high_accuracy), stringResource(R.string.settings_help_high_accuracy), uiState.coreConfig.highAccuracy, { enabled -> viewModel.updateCoreSettings { it.copy(highAccuracy = enabled) } }, { viewModel.updateCoreSettings { it.copy(highAccuracy = defaults.highAccuracy) } })
        SliderRow(stringResource(R.string.settings_core_resolution_label), stringResource(R.string.settings_help_resolution), stringResource(R.string.settings_core_resolution_value, uiState.coreConfig.resolutionMultiplier), { viewModel.updateCoreSettings { it.copy(resolutionMultiplier = defaults.resolutionMultiplier) } }) {
            Slider(value = uiState.coreConfig.resolutionMultiplier, onValueChange = { value -> viewModel.updateCoreSettings { it.copy(resolutionMultiplier = (value * 4).toInt() / 4f) } }, valueRange = 0.5f..4f, steps = 13)
        }
        Chips(stringResource(R.string.settings_core_screen_filter_label), stringResource(R.string.settings_help_screen_filter), { viewModel.updateCoreSettings { it.copy(screenFilter = defaults.screenFilter) } }) {
            TextChip(
                stringResource(R.string.settings_filter_bilinear),
                uiState.coreConfig.screenFilter,
                viewModel
            ) { config, value -> config.copy(screenFilter = value) }
            TextChip(
                stringResource(R.string.settings_filter_nearest),
                uiState.coreConfig.screenFilter,
                viewModel
            ) { config, value -> config.copy(screenFilter = value) }
        }
        SliderRow(stringResource(R.string.settings_core_anisotropic_label), stringResource(R.string.settings_help_anisotropic), stringResource(R.string.settings_core_anisotropic_value, uiState.coreConfig.anisotropicFiltering), { viewModel.updateCoreSettings { it.copy(anisotropicFiltering = defaults.anisotropicFiltering) } }) {
            Slider(value = uiState.coreConfig.anisotropicFiltering.toFloat(), onValueChange = { value -> viewModel.updateCoreSettings { config -> config.copy(anisotropicFiltering = listOf(1, 2, 4, 8, 16).minByOrNull { kotlin.math.abs(it - value.toInt()) } ?: 1) } }, valueRange = 1f..16f, steps = 3)
        }
        Toggle(stringResource(R.string.settings_core_texture_cache), stringResource(R.string.settings_help_texture_cache), uiState.coreConfig.textureCache, { enabled -> viewModel.updateCoreSettings { it.copy(textureCache = enabled) } }, { viewModel.updateCoreSettings { it.copy(textureCache = defaults.textureCache) } })
        Toggle(stringResource(R.string.settings_hashless_texture_cache), stringResource(R.string.settings_help_hashless_texture_cache), uiState.coreConfig.hashlessTextureCache, { enabled -> viewModel.updateCoreSettings { it.copy(hashlessTextureCache = enabled) } }, { viewModel.updateCoreSettings { it.copy(hashlessTextureCache = defaults.hashlessTextureCache) } })
        Toggle(stringResource(R.string.settings_core_async_pipeline), stringResource(R.string.settings_help_async_pipeline), uiState.coreConfig.asyncPipelineCompilation, { enabled -> viewModel.updateCoreSettings { it.copy(asyncPipelineCompilation = enabled) } }, { viewModel.updateCoreSettings { it.copy(asyncPipelineCompilation = defaults.asyncPipelineCompilation) } })
        Toggle(stringResource(R.string.settings_shader_cache), stringResource(R.string.settings_help_shader_cache), uiState.coreConfig.shaderCache, { enabled -> viewModel.updateCoreSettings { it.copy(shaderCache = enabled) } }, { viewModel.updateCoreSettings { it.copy(shaderCache = defaults.shaderCache) } })
        Toggle(stringResource(R.string.settings_core_shader_compilation_notice), stringResource(R.string.settings_help_shader_compilation_notice), uiState.coreConfig.showCompileShaders, { enabled -> viewModel.updateCoreSettings { it.copy(showCompileShaders = enabled) } }, { viewModel.updateCoreSettings { it.copy(showCompileShaders = defaults.showCompileShaders) } })
        Toggle(stringResource(R.string.settings_core_shader_caching_warn), stringResource(R.string.settings_help_shader_caching_warn), uiState.coreConfig.showShaderCacheWarn, { enabled -> viewModel.updateCoreSettings { it.copy(showShaderCacheWarn = enabled) } }, { viewModel.updateCoreSettings { it.copy(showShaderCacheWarn = defaults.showShaderCacheWarn) } })
        Chips(stringResource(R.string.settings_memory_mapping), stringResource(R.string.settings_help_memory_mapping), { viewModel.updateCoreSettings { it.copy(memoryMapping = defaults.memoryMapping) } }) {
            TextChip("disabled", stringResource(R.string.settings_memory_mapping_disabled), uiState.coreConfig.memoryMapping, viewModel) { config, value -> config.copy(memoryMapping = value) }
            TextChip("double-buffer", stringResource(R.string.settings_memory_mapping_double_buffer), uiState.coreConfig.memoryMapping, viewModel) { config, value -> config.copy(memoryMapping = value) }
            TextChip("external-host", stringResource(R.string.settings_memory_mapping_external_host), uiState.coreConfig.memoryMapping, viewModel) { config, value -> config.copy(memoryMapping = value) }
            TextChip("page-table", stringResource(R.string.settings_memory_mapping_page_table), uiState.coreConfig.memoryMapping, viewModel) { config, value -> config.copy(memoryMapping = value) }
            TextChip("native-buffer", stringResource(R.string.settings_memory_mapping_native_buffer), uiState.coreConfig.memoryMapping, viewModel) { config, value -> config.copy(memoryMapping = value) }
        }
        Toggle(stringResource(R.string.settings_core_disable_surface_sync), stringResource(R.string.settings_help_disable_surface_sync), uiState.coreConfig.disableSurfaceSync, { enabled -> viewModel.updateCoreSettings { it.copy(disableSurfaceSync = enabled) } }, { viewModel.updateCoreSettings { it.copy(disableSurfaceSync = defaults.disableSurfaceSync) } })
        Toggle(stringResource(R.string.settings_core_vsync), stringResource(R.string.settings_help_vsync), uiState.coreConfig.vSync, { enabled -> viewModel.updateCoreSettings { it.copy(vSync = enabled) } }, { viewModel.updateCoreSettings { it.copy(vSync = defaults.vSync) } })
        Toggle(stringResource(R.string.settings_core_stretch_display), stringResource(R.string.settings_help_stretch_display), uiState.coreConfig.stretchDisplayArea, { enabled -> viewModel.updateCoreSettings { it.copy(stretchDisplayArea = enabled) } }, { viewModel.updateCoreSettings { it.copy(stretchDisplayArea = defaults.stretchDisplayArea) } })
        Toggle(stringResource(R.string.settings_pixel_perfect_fullscreen), stringResource(R.string.settings_help_pixel_perfect_fullscreen), uiState.coreConfig.fullscreenHdResPixelPerfect, { enabled -> viewModel.updateCoreSettings { it.copy(fullscreenHdResPixelPerfect = enabled) } }, { viewModel.updateCoreSettings { it.copy(fullscreenHdResPixelPerfect = defaults.fullscreenHdResPixelPerfect) } })
        Toggle(stringResource(R.string.settings_import_textures), stringResource(R.string.settings_help_import_textures), uiState.coreConfig.importTextures, { enabled -> viewModel.updateCoreSettings { it.copy(importTextures = enabled) } }, { viewModel.updateCoreSettings { it.copy(importTextures = defaults.importTextures) } })
        Toggle(stringResource(R.string.settings_export_textures), stringResource(R.string.settings_help_export_textures), uiState.coreConfig.exportTextures, { enabled -> viewModel.updateCoreSettings { it.copy(exportTextures = enabled) } }, { viewModel.updateCoreSettings { it.copy(exportTextures = defaults.exportTextures) } })
        Toggle(stringResource(R.string.settings_export_as_png), stringResource(R.string.settings_help_export_as_png), uiState.coreConfig.exportAsPng, { enabled -> viewModel.updateCoreSettings { it.copy(exportAsPng = enabled) } }, { viewModel.updateCoreSettings { it.copy(exportAsPng = defaults.exportAsPng) } })
    }
}

@Composable
private fun GpuDriverStatus(
    selectedDriver: InstalledGpuDriver?,
    backendRenderer: String,
    isActive: Boolean,
    modifier: Modifier = Modifier
) {
    val text = when {
        selectedDriver == null -> stringResource(R.string.settings_gpu_driver_status_system)
        !selectedDriver.isUsable -> stringResource(R.string.settings_gpu_driver_status_broken, selectedDriver.mainLibrary)
        backendRenderer != "Vulkan" -> stringResource(R.string.settings_gpu_driver_status_renderer, backendRenderer)
        else -> stringResource(R.string.settings_gpu_driver_status_active, selectedDriver.mainLibrary)
    }
    val supporting = when {
        selectedDriver == null -> stringResource(R.string.settings_gpu_driver_status_system_desc)
        !selectedDriver.isUsable -> stringResource(R.string.settings_gpu_driver_status_broken_desc)
        !isActive -> stringResource(R.string.settings_gpu_driver_status_renderer_desc)
        else -> stringResource(R.string.settings_gpu_driver_status_active_desc)
    }

    Surface(
        modifier = modifier
            .fillMaxWidth()
            .padding(top = 4.dp),
        shape = RoundedCornerShape(18.dp),
        color = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.22f)
    ) {
        Column(modifier = Modifier.padding(horizontal = SettingsCardInnerPadding, vertical = SettingsCardInnerPadding)) {
            Text(
                text = text,
                style = MaterialTheme.typography.bodyMedium.copy(fontWeight = FontWeight.SemiBold),
                color = MaterialTheme.colorScheme.onSurface
            )
            Text(
                text = supporting,
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
                modifier = Modifier.padding(top = 4.dp)
            )
        }
    }
}

@Composable
private fun AudioTab(uiState: SettingsUiState, defaults: VitaCoreConfig, viewModel: SettingsViewModel, refreshCoreSettingsClick: () -> Unit) {
    SectionCard(title = stringResource(R.string.settings_core_audio_title), contentPadding = androidx.compose.foundation.layout.PaddingValues(SettingsSectionContentPadding)) {
        Chips(stringResource(R.string.settings_core_audio_backend_label), stringResource(R.string.settings_help_audio_backend), { viewModel.updateCoreSettings { it.copy(audioBackend = defaults.audioBackend) } }) {
            TextChip(
                stringResource(R.string.settings_audio_backend_sdl),
                uiState.coreConfig.audioBackend,
                viewModel
            ) { config, value -> config.copy(audioBackend = value) }
            TextChip(
                stringResource(R.string.settings_audio_backend_cubeb),
                uiState.coreConfig.audioBackend,
                viewModel
            ) { config, value -> config.copy(audioBackend = value) }
        }
        SliderRow(stringResource(R.string.settings_core_audio_volume_label), stringResource(R.string.settings_help_audio_volume), stringResource(R.string.settings_core_audio_volume_value, uiState.coreConfig.audioVolume), { viewModel.updateCoreSettings { it.copy(audioVolume = defaults.audioVolume) } }) {
            Slider(value = uiState.coreConfig.audioVolume.toFloat(), onValueChange = { value -> viewModel.updateCoreSettings { it.copy(audioVolume = value.toInt()) } }, valueRange = 0f..100f)
        }
        SliderRow(stringResource(R.string.settings_bgm_volume), stringResource(R.string.settings_help_bgm_volume), stringResource(R.string.settings_bgm_volume_value, uiState.coreConfig.bgmVolume), { viewModel.updateCoreSettings { it.copy(bgmVolume = defaults.bgmVolume) } }) {
            Slider(value = uiState.coreConfig.bgmVolume.toFloat(), onValueChange = { value -> viewModel.updateCoreSettings { it.copy(bgmVolume = value.toInt()) } }, valueRange = 0f..100f)
        }
        Toggle(stringResource(R.string.settings_core_ngs_enable), stringResource(R.string.settings_help_ngs_enable), uiState.coreConfig.ngsEnable, { enabled -> viewModel.updateCoreSettings { it.copy(ngsEnable = enabled) } }, { viewModel.updateCoreSettings { it.copy(ngsEnable = defaults.ngsEnable) } })
        Button(onClick = refreshCoreSettingsClick, modifier = Modifier.padding(top = 12.dp)) { Text(stringResource(R.string.settings_core_reload)) }
    }
}

@Composable
private fun OverlayTab(uiState: SettingsUiState, defaults: VitaCoreConfig, viewModel: SettingsViewModel) {
    SectionCard(title = stringResource(R.string.settings_tab_overlay), contentPadding = androidx.compose.foundation.layout.PaddingValues(SettingsSectionContentPadding)) {
        Toggle(stringResource(R.string.settings_core_performance_overlay), stringResource(R.string.settings_help_performance_overlay), uiState.coreConfig.performanceOverlay, { enabled -> viewModel.updateCoreSettings { it.copy(performanceOverlay = enabled) } }, { viewModel.updateCoreSettings { it.copy(performanceOverlay = defaults.performanceOverlay) } })
        Chips(stringResource(R.string.settings_core_overlay_detail_title), stringResource(R.string.settings_help_performance_overlay_detail), { viewModel.updateCoreSettings { it.copy(performanceOverlayDetail = defaults.performanceOverlayDetail) } }) {
            IntChip(
                0,
                stringResource(R.string.settings_core_overlay_detail_minimum),
                uiState.coreConfig.performanceOverlayDetail,
                viewModel
            ) { config, value -> config.copy(performanceOverlayDetail = value) }
            IntChip(
                1,
                stringResource(R.string.settings_core_overlay_detail_low),
                uiState.coreConfig.performanceOverlayDetail,
                viewModel
            ) { config, value -> config.copy(performanceOverlayDetail = value) }
            IntChip(
                2,
                stringResource(R.string.settings_core_overlay_detail_medium),
                uiState.coreConfig.performanceOverlayDetail,
                viewModel
            ) { config, value -> config.copy(performanceOverlayDetail = value) }
            IntChip(
                3,
                stringResource(R.string.settings_core_overlay_detail_maximum),
                uiState.coreConfig.performanceOverlayDetail,
                viewModel
            ) { config, value -> config.copy(performanceOverlayDetail = value) }
        }
        Chips(stringResource(R.string.settings_core_overlay_position_title), stringResource(R.string.settings_help_performance_overlay_position), { viewModel.updateCoreSettings { it.copy(performanceOverlayPosition = defaults.performanceOverlayPosition) } }) {
            IntChip(
                0,
                stringResource(R.string.settings_core_overlay_position_top_left),
                uiState.coreConfig.performanceOverlayPosition,
                viewModel
            ) { config, value -> config.copy(performanceOverlayPosition = value) }
            IntChip(
                1,
                stringResource(R.string.settings_core_overlay_position_top_center),
                uiState.coreConfig.performanceOverlayPosition,
                viewModel
            ) { config, value -> config.copy(performanceOverlayPosition = value) }
            IntChip(
                2,
                stringResource(R.string.settings_core_overlay_position_top_right),
                uiState.coreConfig.performanceOverlayPosition,
                viewModel
            ) { config, value -> config.copy(performanceOverlayPosition = value) }
            IntChip(
                3,
                stringResource(R.string.settings_core_overlay_position_bottom_left),
                uiState.coreConfig.performanceOverlayPosition,
                viewModel
            ) { config, value -> config.copy(performanceOverlayPosition = value) }
            IntChip(
                4,
                stringResource(R.string.settings_core_overlay_position_bottom_center),
                uiState.coreConfig.performanceOverlayPosition,
                viewModel
            ) { config, value -> config.copy(performanceOverlayPosition = value) }
            IntChip(
                5,
                stringResource(R.string.settings_core_overlay_position_bottom_right),
                uiState.coreConfig.performanceOverlayPosition,
                viewModel
            ) { config, value -> config.copy(performanceOverlayPosition = value) }
        }
        Toggle(stringResource(R.string.settings_core_gamepad_overlay), stringResource(R.string.settings_help_gamepad_overlay), uiState.coreConfig.enableGamepadOverlay, { enabled -> viewModel.updateCoreSettings { it.copy(enableGamepadOverlay = enabled) } }, { viewModel.updateCoreSettings { it.copy(enableGamepadOverlay = defaults.enableGamepadOverlay) } })
        Toggle(stringResource(R.string.settings_show_touch_switch), stringResource(R.string.settings_help_show_touch_switch), uiState.coreConfig.overlayShowTouchSwitch, { enabled -> viewModel.updateCoreSettings { it.copy(overlayShowTouchSwitch = enabled) } }, { viewModel.updateCoreSettings { it.copy(overlayShowTouchSwitch = defaults.overlayShowTouchSwitch) } })
        SliderRow(stringResource(R.string.settings_core_overlay_scale_label), stringResource(R.string.settings_help_overlay_scale), stringResource(R.string.settings_core_overlay_scale_value, uiState.coreConfig.overlayScale), { viewModel.updateCoreSettings { it.copy(overlayScale = defaults.overlayScale) } }) {
            Slider(value = uiState.coreConfig.overlayScale, onValueChange = { value -> viewModel.updateCoreSettings { it.copy(overlayScale = (value * 10).toInt() / 10f) } }, valueRange = 0.5f..2f, steps = 14)
        }
        SliderRow(stringResource(R.string.settings_core_overlay_opacity_label), stringResource(R.string.settings_help_overlay_opacity), stringResource(R.string.settings_core_overlay_opacity_value, uiState.coreConfig.overlayOpacity), { viewModel.updateCoreSettings { it.copy(overlayOpacity = defaults.overlayOpacity) } }) {
            Slider(value = uiState.coreConfig.overlayOpacity.toFloat(), onValueChange = { value -> viewModel.updateCoreSettings { it.copy(overlayOpacity = value.toInt()) } }, valueRange = 10f..100f, steps = 8)
        }
    }
}

@Composable
private fun ControlsTab(uiState: SettingsUiState, defaults: VitaCoreConfig, viewModel: SettingsViewModel) {
    SectionCard(title = stringResource(R.string.settings_tab_controls), contentPadding = androidx.compose.foundation.layout.PaddingValues(SettingsSectionContentPadding)) {
        Toggle(stringResource(R.string.settings_core_touchpad_cursor), stringResource(R.string.settings_help_touchpad_cursor), uiState.coreConfig.showTouchpadCursor, { enabled -> viewModel.updateCoreSettings { it.copy(showTouchpadCursor = enabled) } }, { viewModel.updateCoreSettings { it.copy(showTouchpadCursor = defaults.showTouchpadCursor) } })
        Toggle(stringResource(R.string.settings_core_disable_motion), stringResource(R.string.settings_help_disable_motion), uiState.coreConfig.disableMotion, { enabled -> viewModel.updateCoreSettings { it.copy(disableMotion = enabled) } }, { viewModel.updateCoreSettings { it.copy(disableMotion = defaults.disableMotion) } })
        SliderRow(stringResource(R.string.settings_core_analog_multiplier_label), stringResource(R.string.settings_help_analog_multiplier), stringResource(R.string.settings_core_analog_multiplier_value, uiState.coreConfig.analogMultiplier), { viewModel.updateCoreSettings { it.copy(analogMultiplier = defaults.analogMultiplier) } }) {
            Slider(value = uiState.coreConfig.analogMultiplier, onValueChange = { value -> viewModel.updateCoreSettings { it.copy(analogMultiplier = (value * 10).toInt() / 10f) } }, valueRange = 0.5f..2f, steps = 14)
        }
    }
}

@Composable
private fun SystemTab(uiState: SettingsUiState, defaults: VitaCoreConfig, viewModel: SettingsViewModel) {
    SectionCard(title = stringResource(R.string.settings_tab_system), contentPadding = androidx.compose.foundation.layout.PaddingValues(SettingsSectionContentPadding)) {
        Toggle(stringResource(R.string.settings_core_fps_hack), stringResource(R.string.settings_help_fps_hack), uiState.coreConfig.fpsHack, { enabled -> viewModel.updateCoreSettings { it.copy(fpsHack = enabled) } }, { viewModel.updateCoreSettings { it.copy(fpsHack = defaults.fpsHack) } })
        Toggle(stringResource(R.string.settings_turbo_mode), stringResource(R.string.settings_help_turbo_mode), uiState.coreConfig.turboMode, { enabled -> viewModel.updateCoreSettings { it.copy(turboMode = enabled) } }, { viewModel.updateCoreSettings { it.copy(turboMode = defaults.turboMode) } })
        Toggle(stringResource(R.string.settings_boot_apps_fullscreen), stringResource(R.string.settings_help_boot_apps_fullscreen), uiState.coreConfig.bootAppsFullScreen, { enabled -> viewModel.updateCoreSettings { it.copy(bootAppsFullScreen = enabled) } }, { viewModel.updateCoreSettings { it.copy(bootAppsFullScreen = defaults.bootAppsFullScreen) } })
        Toggle(stringResource(R.string.settings_http_features), stringResource(R.string.settings_help_http_features), uiState.coreConfig.httpEnable, { enabled -> viewModel.updateCoreSettings { it.copy(httpEnable = enabled) } }, { viewModel.updateCoreSettings { it.copy(httpEnable = defaults.httpEnable) } })
        Toggle(stringResource(R.string.settings_psn_signed_in), stringResource(R.string.settings_help_psn_signed_in), uiState.coreConfig.psnSignedIn, { enabled -> viewModel.updateCoreSettings { it.copy(psnSignedIn = enabled) } }, { viewModel.updateCoreSettings { it.copy(psnSignedIn = defaults.psnSignedIn) } })
        SliderRow(stringResource(R.string.settings_file_loading_delay), stringResource(R.string.settings_help_file_loading_delay), stringResource(R.string.settings_file_loading_delay_value, uiState.coreConfig.fileLoadingDelay), { viewModel.updateCoreSettings { it.copy(fileLoadingDelay = defaults.fileLoadingDelay) } }) {
            Slider(value = uiState.coreConfig.fileLoadingDelay.toFloat(), onValueChange = { value -> viewModel.updateCoreSettings { it.copy(fileLoadingDelay = value.toInt().coerceIn(0, 500)) } }, valueRange = 0f..500f, steps = 24)
        }
    }
}

@Composable
private fun AdvancedTab(uiState: SettingsUiState, defaults: VitaCoreConfig, viewModel: SettingsViewModel) {
    SectionCard(title = stringResource(R.string.settings_tab_advanced), contentPadding = androidx.compose.foundation.layout.PaddingValues(SettingsSectionContentPadding)) {
        Toggle(stringResource(R.string.settings_validation_layer), stringResource(R.string.settings_help_validation_layer), uiState.coreConfig.validationLayer, { enabled -> viewModel.updateCoreSettings { it.copy(validationLayer = enabled) } }, { viewModel.updateCoreSettings { it.copy(validationLayer = defaults.validationLayer) } })
        Toggle(stringResource(R.string.settings_log_active_shaders), stringResource(R.string.settings_help_log_active_shaders), uiState.coreConfig.logActiveShaders, { enabled -> viewModel.updateCoreSettings { it.copy(logActiveShaders = enabled) } }, { viewModel.updateCoreSettings { it.copy(logActiveShaders = defaults.logActiveShaders) } })
        Toggle(stringResource(R.string.settings_log_uniforms), stringResource(R.string.settings_help_log_uniforms), uiState.coreConfig.logUniforms, { enabled -> viewModel.updateCoreSettings { it.copy(logUniforms = enabled) } }, { viewModel.updateCoreSettings { it.copy(logUniforms = defaults.logUniforms) } })
        Toggle(stringResource(R.string.settings_compatibility_warnings), stringResource(R.string.settings_help_compatibility_warnings), uiState.coreConfig.logCompatWarn, { enabled -> viewModel.updateCoreSettings { it.copy(logCompatWarn = enabled) } }, { viewModel.updateCoreSettings { it.copy(logCompatWarn = defaults.logCompatWarn) } })
        Toggle(stringResource(R.string.settings_archive_install_log), stringResource(R.string.settings_help_archive_install_log), uiState.coreConfig.archiveLog, { enabled -> viewModel.updateCoreSettings { it.copy(archiveLog = enabled) } }, { viewModel.updateCoreSettings { it.copy(archiveLog = defaults.archiveLog) } })
        Toggle(stringResource(R.string.settings_color_surface_debug), stringResource(R.string.settings_help_color_surface_debug), uiState.coreConfig.colorSurfaceDebug, { enabled -> viewModel.updateCoreSettings { it.copy(colorSurfaceDebug = enabled) } }, { viewModel.updateCoreSettings { it.copy(colorSurfaceDebug = defaults.colorSurfaceDebug) } })
        Toggle(stringResource(R.string.settings_spirv_shader_mode), stringResource(R.string.settings_help_spirv_shader_mode), uiState.coreConfig.spirvShader, { enabled -> viewModel.updateCoreSettings { it.copy(spirvShader = enabled) } }, { viewModel.updateCoreSettings { it.copy(spirvShader = defaults.spirvShader) } })
        Toggle(stringResource(R.string.settings_discord_rich_presence), stringResource(R.string.settings_help_discord_rich_presence), uiState.coreConfig.discordRichPresence, { enabled -> viewModel.updateCoreSettings { it.copy(discordRichPresence = enabled) } }, { viewModel.updateCoreSettings { it.copy(discordRichPresence = defaults.discordRichPresence) } })
        Toggle(stringResource(R.string.settings_check_for_updates), stringResource(R.string.settings_help_check_for_updates), uiState.coreConfig.checkForUpdates, { enabled -> viewModel.updateCoreSettings { it.copy(checkForUpdates = enabled) } }, { viewModel.updateCoreSettings { it.copy(checkForUpdates = defaults.checkForUpdates) } })
    }
}

@Composable
private fun StorageTab(uiState: SettingsUiState, changeFolderClick: () -> Unit, clearFolderClick: () -> Unit) {
    SectionCard(title = stringResource(R.string.settings_packages_folder), contentPadding = androidx.compose.foundation.layout.PaddingValues(SettingsSectionContentPadding)) {
        Text(
            text = stringResource(R.string.settings_help_packages_folder),
            color = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.82f)
        )
        Text(
            text = uiState.packagesFolderPath ?: stringResource(R.string.settings_packages_not_selected),
            color = MaterialTheme.colorScheme.onSurface,
            modifier = Modifier.padding(top = 6.dp)
        )
        Button(onClick = changeFolderClick, modifier = Modifier.padding(top = 12.dp)) { Text(stringResource(R.string.settings_change_folder)) }
        Button(onClick = clearFolderClick, modifier = Modifier.padding(top = 8.dp)) { Text(stringResource(R.string.settings_clear_folder)) }
    }
    SectionCard(title = stringResource(R.string.settings_storage_title), contentPadding = androidx.compose.foundation.layout.PaddingValues(SettingsSectionContentPadding)) {
        Text(text = stringResource(R.string.settings_storage_body), color = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.82f))
        Text(text = uiState.storagePath, modifier = Modifier.padding(top = 12.dp), color = MaterialTheme.colorScheme.primary)
    }
}

@Composable
private fun AboutTab() {
    val context = LocalContext.current
    val uriHandler = LocalUriHandler.current
    val packageInfo = remember {
        try {
            context.packageManager.getPackageInfo(context.packageName, 0)
        } catch (_: Exception) {
            null
        }
    }
    val versionName = packageInfo?.versionName ?: "1.0.0"

    Column(
        modifier = Modifier.fillMaxWidth(),
        verticalArrangement = Arrangement.spacedBy(16.dp)
    ) {
        Text(
            text = stringResource(R.string.settings_about_title),
            style = MaterialTheme.typography.titleLarge.copy(fontWeight = FontWeight.SemiBold),
            color = MaterialTheme.colorScheme.onBackground,
            modifier = Modifier.padding(horizontal = ScreenHorizontalPadding)
        )

        Column(
            modifier = Modifier.padding(horizontal = ScreenHorizontalPadding),
            verticalArrangement = Arrangement.spacedBy(8.dp)
        ) {
            LinkItem(
                icon = Icons.Rounded.Info,
                title = stringResource(R.string.settings_tab_about),
                subtitle = versionName,
                onClick = {}
            )
            LinkItem(
                icon = Icons.Rounded.Memory,
                title = stringResource(R.string.settings_emulation_core),
                subtitle = stringResource(R.string.settings_based_on_vita3k),
                onClick = {}
            )
        }

        Column(modifier = Modifier.padding(horizontal = ScreenHorizontalPadding)) {
            Text(
                text = stringResource(R.string.settings_about_emucorev),
                style = MaterialTheme.typography.titleMedium.copy(fontWeight = FontWeight.SemiBold),
                color = MaterialTheme.colorScheme.onBackground
            )
            Text(
                text = stringResource(R.string.settings_about_body),
                style = MaterialTheme.typography.bodyMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
                modifier = Modifier.padding(top = 4.dp)
            )
        }

        Column(modifier = Modifier.padding(horizontal = ScreenHorizontalPadding)) {
            Text(
                text = stringResource(R.string.settings_developer),
                style = MaterialTheme.typography.titleMedium.copy(fontWeight = FontWeight.SemiBold),
                color = MaterialTheme.colorScheme.onBackground
            )
            Text(
                text = stringResource(R.string.settings_created_by_sbro),
                style = MaterialTheme.typography.bodyMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
                modifier = Modifier.padding(top = 4.dp)
            )
            Text(
                text = stringResource(R.string.settings_play_store_profile),
                style = MaterialTheme.typography.bodyMedium.copy(fontWeight = FontWeight.Medium),
                color = MaterialTheme.colorScheme.primary,
                modifier = Modifier
                    .padding(top = 4.dp)
                    .clickable { uriHandler.openUri("https://play.google.com/store/apps/dev?id=7136622298887775989") }
            )
        }

        Column(modifier = Modifier.padding(horizontal = ScreenHorizontalPadding)) {
            Text(
                text = stringResource(R.string.settings_core_source_code),
                style = MaterialTheme.typography.titleMedium.copy(fontWeight = FontWeight.SemiBold),
                color = MaterialTheme.colorScheme.onBackground
            )
            Text(
                text = stringResource(R.string.settings_core_source_description),
                style = MaterialTheme.typography.bodyMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
                modifier = Modifier.padding(top = 4.dp)
            )
            Text(
                text = stringResource(R.string.settings_open_github),
                style = MaterialTheme.typography.bodyMedium.copy(fontWeight = FontWeight.Medium),
                color = MaterialTheme.colorScheme.primary,
                modifier = Modifier
                    .padding(top = 4.dp)
                    .clickable { uriHandler.openUri("https://github.com/Vita3K/Vita3K") }
            )
        }
    }
}

@Composable
private fun LinkItem(
    icon: ImageVector,
    title: String,
    subtitle: String,
    onClick: () -> Unit
) {
    Surface(
        modifier = Modifier.fillMaxWidth(),
        shape = RoundedCornerShape(18.dp),
        color = MaterialTheme.colorScheme.surfaceContainerHigh,
        tonalElevation = 1.dp,
        shadowElevation = 2.dp,
        border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.6f)),
        onClick = onClick
    ) {
        Row(
            modifier = Modifier.padding(horizontal = SettingsCardInnerPadding, vertical = SettingsCardInnerPadding),
            verticalAlignment = Alignment.CenterVertically
        ) {
            Box(
                modifier = Modifier
                    .size(38.dp)
                    .clip(RoundedCornerShape(12.dp))
                    .background(MaterialTheme.colorScheme.primary.copy(alpha = 0.1f)),
                contentAlignment = Alignment.Center
            ) {
                Icon(
                    imageVector = icon,
                    contentDescription = null,
                    tint = MaterialTheme.colorScheme.primary,
                    modifier = Modifier.size(20.dp)
                )
            }
            Spacer(modifier = Modifier.width(14.dp))
            Column(modifier = Modifier.weight(1f)) {
                Text(
                    text = title,
                    style = MaterialTheme.typography.bodyLarge,
                    color = MaterialTheme.colorScheme.onSurface
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

@Composable
private fun Toggle(title: String, description: String, checked: Boolean, onCheckedChange: (Boolean) -> Unit, onResetDefault: () -> Unit) =
    SettingToggleRow(title = title, description = description, checked = checked, onCheckedChange = onCheckedChange, onResetDefault = onResetDefault)

@Composable
private fun Chips(title: String, description: String, onResetDefault: () -> Unit, content: @Composable FlowRowScope.() -> Unit) =
    SettingChoiceRow(title = title, description = description, onResetDefault = onResetDefault, content = content)

@Composable
private fun SliderRow(title: String, description: String, valueText: String, onResetDefault: () -> Unit, content: @Composable () -> Unit) =
    SettingSliderRow(title = title, description = description, valueText = valueText, onResetDefault = onResetDefault, slider = content)

@Composable
private fun AppLanguageSettingRow(
    selectedLanguage: AppLanguage,
    onClick: () -> Unit
) {
    Surface(
        modifier = Modifier
            .fillMaxWidth()
            .padding(horizontal = SettingsSectionRowPadding),
        shape = RoundedCornerShape(18.dp),
        color = MaterialTheme.colorScheme.surfaceContainerHigh,
        tonalElevation = 1.dp,
        shadowElevation = 2.dp,
        border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.62f)),
        onClick = onClick
    ) {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .padding(horizontal = SettingsCardInnerPadding, vertical = SettingsCardInnerPadding),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(14.dp)
        ) {
            Box(
                modifier = Modifier
                    .size(40.dp)
                    .clip(RoundedCornerShape(14.dp))
                    .background(MaterialTheme.colorScheme.primary.copy(alpha = 0.10f)),
                contentAlignment = Alignment.Center
            ) {
                Icon(
                    imageVector = Icons.Rounded.Language,
                    contentDescription = null,
                    tint = MaterialTheme.colorScheme.primary,
                    modifier = Modifier.size(20.dp)
                )
            }
            Column(modifier = Modifier.weight(1f)) {
                Text(
                    text = stringResource(R.string.settings_app_language),
                    style = MaterialTheme.typography.bodyLarge,
                    color = MaterialTheme.colorScheme.onSurface
                )
                Text(
                    text = appLanguageLabel(selectedLanguage),
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
            }
        }
    }
}

@Composable
private fun VitaLanguageSettingRow(
    selectedLanguage: Int,
    onClick: () -> Unit
) {
    Surface(
        modifier = Modifier
            .fillMaxWidth()
            .padding(horizontal = SettingsSectionRowPadding),
        shape = RoundedCornerShape(18.dp),
        color = MaterialTheme.colorScheme.surfaceContainerHigh,
        tonalElevation = 1.dp,
        shadowElevation = 2.dp,
        border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.62f)),
        onClick = onClick
    ) {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .padding(horizontal = SettingsCardInnerPadding, vertical = SettingsCardInnerPadding),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(14.dp)
        ) {
            Box(
                modifier = Modifier
                    .size(40.dp)
                    .clip(RoundedCornerShape(14.dp))
                    .background(MaterialTheme.colorScheme.primary.copy(alpha = 0.10f)),
                contentAlignment = Alignment.Center
            ) {
                Icon(
                    imageVector = Icons.Rounded.Language,
                    contentDescription = null,
                    tint = MaterialTheme.colorScheme.primary,
                    modifier = Modifier.size(20.dp)
                )
            }
            Column(modifier = Modifier.weight(1f)) {
                Text(
                    text = stringResource(R.string.settings_system_language),
                    style = MaterialTheme.typography.bodyLarge,
                    color = MaterialTheme.colorScheme.onSurface
                )
                Text(
                    text = vitaLanguageLabel(selectedLanguage),
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
            }
        }
    }
}

@Composable
private fun appLanguageLabel(language: AppLanguage): String = when (language) {
    AppLanguage.SYSTEM -> stringResource(R.string.settings_app_language_system)
    AppLanguage.ENGLISH -> stringResource(R.string.settings_app_language_english)
    AppLanguage.RUSSIAN -> stringResource(R.string.settings_app_language_russian)
    AppLanguage.UKRAINIAN -> stringResource(R.string.settings_app_language_ukrainian)
    AppLanguage.SPANISH -> stringResource(R.string.settings_app_language_spanish)
    AppLanguage.FRENCH -> stringResource(R.string.settings_app_language_french)
    AppLanguage.GERMAN -> stringResource(R.string.settings_app_language_german)
    AppLanguage.PORTUGUESE -> stringResource(R.string.settings_app_language_portuguese)
    AppLanguage.CHINESE -> stringResource(R.string.settings_app_language_chinese_traditional)
    AppLanguage.HINDI -> stringResource(R.string.settings_app_language_hindi)
    AppLanguage.ITALIAN -> stringResource(R.string.settings_app_language_italian)
}

@Composable
private fun vitaLanguageLabel(languageValue: Int): String {
    return VitaSystemLanguages.firstOrNull { it.value == languageValue }?.nativeLabel
        ?: stringResource(R.string.settings_vita_language_unknown)
}

@Composable
private fun ButtonChip(
    value: Int,
    label: String,
    uiState: SettingsUiState,
    viewModel: SettingsViewModel
) {
    FilterChip(
        selected = uiState.coreConfig.sysButton == value,
        onClick = { viewModel.updateCoreSettings { it.copy(sysButton = value) } },
        colors = appFilterChipColors(),
        label = { Text(label) }
    )
}

@Composable
private fun ModeChip(
    value: Int,
    label: String,
    uiState: SettingsUiState,
    viewModel: SettingsViewModel
) {
    FilterChip(
        selected = uiState.coreConfig.modulesMode == value,
        onClick = { viewModel.updateCoreSettings { it.copy(modulesMode = value) } },
        colors = appFilterChipColors(),
        label = { Text(label) }
    )
}

@Composable
private fun IntChip(
    value: Int,
    label: String,
    current: Int,
    viewModel: SettingsViewModel,
    transform: (VitaCoreConfig, Int) -> VitaCoreConfig
) {
    FilterChip(
        selected = current == value,
        onClick = { viewModel.updateCoreSettings { config -> transform(config, value) } },
        colors = appFilterChipColors(),
        label = { Text(label) }
    )
}

@Composable
private fun TextChip(
    value: String,
    current: String,
    viewModel: SettingsViewModel,
    transform: (VitaCoreConfig, String) -> VitaCoreConfig
) = TextChip(
    value = value,
    label = value,
    current = current,
    viewModel = viewModel,
    transform = transform
)

@Composable
private fun TextChip(
    value: String,
    label: String,
    current: String,
    viewModel: SettingsViewModel,
    transform: (VitaCoreConfig, String) -> VitaCoreConfig
) {
    FilterChip(
        selected = current == value,
        onClick = { viewModel.updateCoreSettings { config -> transform(config, value) } },
        colors = appFilterChipColors(),
        label = { Text(label) }
    )
}

@Composable
private fun appFilterChipColors() = FilterChipDefaults.filterChipColors(
    containerColor = MaterialTheme.colorScheme.surfaceContainerHigh,
    labelColor = MaterialTheme.colorScheme.onSurface,
    iconColor = MaterialTheme.colorScheme.onSurfaceVariant,
    selectedContainerColor = MaterialTheme.colorScheme.secondaryContainer,
    selectedLabelColor = MaterialTheme.colorScheme.onSecondaryContainer,
    selectedLeadingIconColor = MaterialTheme.colorScheme.onSecondaryContainer
)

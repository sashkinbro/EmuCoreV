package com.sbro.emucorev.ui.gamemanager

import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.background
import androidx.compose.foundation.combinedClickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.asPaddingValues
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.navigationBarsPadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.statusBarsIgnoringVisibility
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.LazyRow
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.RestartAlt
import androidx.compose.material.icons.rounded.SportsEsports
import androidx.compose.material3.FilterChip
import androidx.compose.material3.FilterChipDefaults
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Slider
import androidx.compose.material3.Surface
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.LifecycleEventObserver
import androidx.lifecycle.compose.LocalLifecycleOwner
import androidx.lifecycle.viewmodel.compose.viewModel
import com.sbro.emucorev.R
import com.sbro.emucorev.core.FrameLimit
import com.sbro.emucorev.core.GpuDriverCompatibility
import com.sbro.emucorev.core.InstalledGpuDriver
import com.sbro.emucorev.core.VitaCoreConfig
import com.sbro.emucorev.data.InstalledVitaGame
import com.sbro.emucorev.ui.common.LocalImage
import com.sbro.emucorev.ui.common.PremiumLoadingAnimation
import com.sbro.emucorev.ui.common.ScreenTopBar
import com.sbro.emucorev.ui.common.SectionCard
import com.sbro.emucorev.ui.common.SettingHelpButton
import com.sbro.emucorev.ui.theme.CardContentPadding
import com.sbro.emucorev.ui.theme.ScreenContentBottomPadding
import com.sbro.emucorev.ui.theme.ScreenHorizontalPadding
import kotlin.math.roundToInt

@OptIn(ExperimentalLayoutApi::class)
@Composable
fun GameManagerScreen(
    initialTitleId: String? = null,
    onMenuClick: (() -> Unit)? = null,
    onBackClick: (() -> Unit)? = null,
    onOpenGpuDriverManager: (String?) -> Unit = {},
    viewModel: GameManagerViewModel = viewModel()
) {
    val uiState by viewModel.uiState.collectAsState()
    val lifecycleOwner = LocalLifecycleOwner.current
    val resumeTitleId by rememberUpdatedState(initialTitleId ?: uiState.selectedTitleId)
    val topInset = WindowInsets.statusBarsIgnoringVisibility.asPaddingValues().calculateTopPadding()
    var selectedTab by remember { mutableStateOf(GameManagerTab.Graphics) }

    androidx.compose.runtime.LaunchedEffect(initialTitleId) {
        initialTitleId?.takeIf(String::isNotBlank)?.let(viewModel::selectGame)
    }

    DisposableEffect(lifecycleOwner, initialTitleId) {
        val observer = LifecycleEventObserver { _, event ->
            if (event == Lifecycle.Event.ON_RESUME) {
                viewModel.refresh(resumeTitleId)
            }
        }
        lifecycleOwner.lifecycle.addObserver(observer)
        onDispose { lifecycleOwner.lifecycle.removeObserver(observer) }
    }

    LazyColumn(
        modifier = Modifier
            .fillMaxSize()
            .background(MaterialTheme.colorScheme.background)
            .navigationBarsPadding(),
        contentPadding = androidx.compose.foundation.layout.PaddingValues(
            top = topInset,
            bottom = ScreenContentBottomPadding
        ),
        verticalArrangement = Arrangement.spacedBy(14.dp)
    ) {
        item {
            ScreenTopBar(
                title = stringResource(R.string.game_manager_title),
                onBackClick = onBackClick,
                onMenuClick = onMenuClick,
                modifier = Modifier.padding(horizontal = ScreenHorizontalPadding)
            )
        }

        if (uiState.isLoading) {
            item {
                Box(
                    modifier = Modifier
                        .fillMaxWidth()
                        .padding(vertical = 64.dp),
                    contentAlignment = Alignment.Center
                ) {
                    PremiumLoadingAnimation(size = 64.dp)
                }
            }
        } else if (uiState.games.isEmpty()) {
            item {
                EmptyGameManagerState()
            }
        } else {
            item {
                GamePicker(
                    games = uiState.games,
                    selectedTitleId = uiState.selectedTitleId,
                    selectedGame = uiState.selectedGame,
                    hasCustomProfile = uiState.hasCustomProfile,
                    onReset = viewModel::resetSelectedToGlobal,
                    onSelect = viewModel::selectGame
                )
            }
            item {
                GameManagerTabs(selectedTab = selectedTab, onSelected = { selectedTab = it })
            }
            item {
                when (selectedTab) {
                    GameManagerTab.Graphics -> GraphicsProfileSection(
                        config = uiState.config,
                        defaults = uiState.defaults,
                        installedGpuDrivers = uiState.installedGpuDrivers,
                        customDriverOverride = uiState.customDriverOverride,
                        onOpenGpuDriverManager = { onOpenGpuDriverManager(uiState.selectedTitleId) },
                        onUpdate = viewModel::updateSelected,
                        onDriverOverrideSelected = viewModel::selectCustomDriverOverride
                    )
                    GameManagerTab.Performance -> PerformanceProfileSection(uiState.config, uiState.defaults, viewModel::updateSelected)
                    GameManagerTab.Audio -> AudioProfileSection(uiState.config, uiState.defaults, viewModel::updateSelected)
                    GameManagerTab.Overlay -> OverlayProfileSection(uiState.config, uiState.defaults, viewModel::updateSelected)
                    GameManagerTab.Gamepad -> GamepadProfileSection(uiState.config, uiState.defaults, viewModel::updateSelected)
                }
            }
        }
    }
}

private enum class GameManagerTab {
    Graphics,
    Performance,
    Audio,
    Overlay,
    Gamepad
}

@Composable
private fun GameManagerTabs(
    selectedTab: GameManagerTab,
    onSelected: (GameManagerTab) -> Unit
) {
    LazyRow(
        horizontalArrangement = Arrangement.spacedBy(8.dp),
        contentPadding = androidx.compose.foundation.layout.PaddingValues(horizontal = ScreenHorizontalPadding)
    ) {
        items(GameManagerTab.entries, key = { it.name }) { tab ->
            FilterChip(
                selected = selectedTab == tab,
                onClick = { onSelected(tab) },
                colors = FilterChipDefaults.filterChipColors(
                    selectedContainerColor = MaterialTheme.colorScheme.secondaryContainer,
                    selectedLabelColor = MaterialTheme.colorScheme.onSecondaryContainer
                ),
                label = {
                    Text(
                        text = when (tab) {
                            GameManagerTab.Graphics -> stringResource(R.string.settings_tab_graphics)
                            GameManagerTab.Performance -> stringResource(R.string.game_manager_system_performance)
                            GameManagerTab.Audio -> stringResource(R.string.settings_core_audio_title)
                            GameManagerTab.Overlay -> stringResource(R.string.settings_tab_overlay)
                            GameManagerTab.Gamepad -> stringResource(R.string.settings_gamepad_section)
                        }
                    )
                }
            )
        }
    }
}

@Composable
private fun EmptyGameManagerState() {
    Box(
        modifier = Modifier
            .fillMaxWidth()
            .height(360.dp),
        contentAlignment = Alignment.Center
    ) {
        Column(
            horizontalAlignment = Alignment.CenterHorizontally,
            verticalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            Icon(
                imageVector = Icons.Rounded.SportsEsports,
                contentDescription = null,
                modifier = Modifier.size(48.dp),
                tint = MaterialTheme.colorScheme.primary
            )
            Text(
                text = stringResource(R.string.game_manager_empty_title),
                style = MaterialTheme.typography.titleLarge.copy(fontWeight = FontWeight.Bold),
                color = MaterialTheme.colorScheme.onBackground,
                textAlign = TextAlign.Center
            )
            Text(
                text = stringResource(R.string.game_manager_empty_body),
                style = MaterialTheme.typography.bodyMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
                textAlign = TextAlign.Center
            )
        }
    }
}

@Composable
private fun GamePicker(
    games: List<InstalledVitaGame>,
    selectedTitleId: String?,
    selectedGame: InstalledVitaGame?,
    hasCustomProfile: Boolean,
    onReset: () -> Unit,
    onSelect: (String) -> Unit
) {
    Column(verticalArrangement = Arrangement.spacedBy(10.dp)) {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .padding(horizontal = ScreenHorizontalPadding),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            Column(modifier = Modifier.weight(1f)) {
                Text(
                    text = stringResource(R.string.game_manager_choose_game),
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
            IconButton(
                onClick = onReset,
                enabled = hasCustomProfile
            ) {
                Icon(
                    imageVector = Icons.Rounded.RestartAlt,
                    contentDescription = stringResource(R.string.game_manager_reset_global)
                )
            }
        }
        LazyRow(
            horizontalArrangement = Arrangement.spacedBy(12.dp),
            contentPadding = androidx.compose.foundation.layout.PaddingValues(horizontal = ScreenHorizontalPadding)
        ) {
            items(games, key = { it.titleId }) { game ->
                val selected = game.titleId == selectedTitleId
                Surface(
                    onClick = { onSelect(game.titleId) },
                    shape = RoundedCornerShape(18.dp),
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
                            shape = RoundedCornerShape(12.dp),
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

@Composable
private fun GraphicsProfileSection(
    config: VitaCoreConfig,
    defaults: VitaCoreConfig,
    installedGpuDrivers: List<InstalledGpuDriver>,
    customDriverOverride: String?,
    onOpenGpuDriverManager: () -> Unit,
    onUpdate: ((VitaCoreConfig) -> VitaCoreConfig) -> Unit,
    onDriverOverrideSelected: (String?) -> Unit
) {
    SectionCard(title = stringResource(R.string.settings_tab_graphics)) {
        ChoiceRow(stringResource(R.string.settings_core_renderer_label), stringResource(R.string.settings_help_renderer), config.backendRenderer, listOf("Vulkan", "OpenGL"), { onUpdate { it.copy(backendRenderer = defaults.backendRenderer) } }) {
            onUpdate { cfg -> cfg.copy(backendRenderer = it) }
        }
        ToggleRow(stringResource(R.string.settings_use_angle), config.useAngle, stringResource(R.string.settings_help_use_angle), { onUpdate { it.copy(useAngle = defaults.useAngle) } }, enabled = config.backendRenderer == "OpenGL") { onUpdate { cfg -> cfg.copy(useAngle = it) } }
        if (remember { GpuDriverCompatibility.supportsAdrenoToolsCustomDrivers() }) {
            GpuDriverChoiceRow(
                effectiveDriverName = config.customDriverName,
                globalDriverName = defaults.customDriverName,
                customDriverOverride = customDriverOverride,
                backendRenderer = config.backendRenderer,
                installedGpuDrivers = installedGpuDrivers,
                onOpenGpuDriverManager = onOpenGpuDriverManager,
                onReset = { onDriverOverrideSelected(null) },
                onSelected = onDriverOverrideSelected
            )
        }
        SliderRow(
            title = stringResource(R.string.settings_core_resolution_label),
            description = stringResource(R.string.settings_help_resolution),
            valueText = stringResource(R.string.settings_core_resolution_value, config.resolutionMultiplier),
            value = config.resolutionMultiplier,
            valueRange = 0.5f..4f,
            steps = 13,
            onReset = { onUpdate { it.copy(resolutionMultiplier = defaults.resolutionMultiplier) } },
            onChange = { value -> onUpdate { it.copy(resolutionMultiplier = (value * 4).toInt() / 4f) } }
        )
        SliderRow(
            title = stringResource(R.string.settings_core_anisotropic_label),
            description = stringResource(R.string.settings_help_anisotropic),
            valueText = stringResource(R.string.settings_core_anisotropic_value, config.anisotropicFiltering),
            value = config.anisotropicFiltering.toFloat(),
            valueRange = 1f..16f,
            steps = 3,
            onReset = { onUpdate { it.copy(anisotropicFiltering = defaults.anisotropicFiltering) } },
            onChange = { value -> onUpdate { cfg -> cfg.copy(anisotropicFiltering = listOf(1, 2, 4, 8, 16).minByOrNull { kotlin.math.abs(it - value.toInt()) } ?: 1) } }
        )
        ChoiceRow(stringResource(R.string.settings_core_screen_filter_label), stringResource(R.string.settings_help_screen_filter), config.screenFilter, listOf("Bilinear", "Nearest"), { onUpdate { it.copy(screenFilter = defaults.screenFilter) } }) {
            onUpdate { cfg -> cfg.copy(screenFilter = it) }
        }
        ChoiceRow(stringResource(R.string.settings_memory_mapping), stringResource(R.string.settings_help_memory_mapping), config.memoryMapping, listOf("disabled", "double-buffer", "external-host", "page-table", "native-buffer"), { onUpdate { it.copy(memoryMapping = defaults.memoryMapping) } }) {
            onUpdate { cfg -> cfg.copy(memoryMapping = it) }
        }
        ToggleRow(stringResource(R.string.settings_core_high_accuracy), config.highAccuracy, stringResource(R.string.settings_help_high_accuracy), { onUpdate { it.copy(highAccuracy = defaults.highAccuracy) } }) { onUpdate { cfg -> cfg.copy(highAccuracy = it) } }
        ToggleRow(stringResource(R.string.settings_core_vsync), config.vSync, stringResource(R.string.settings_help_vsync), { onUpdate { it.copy(vSync = defaults.vSync) } }) { onUpdate { cfg -> cfg.copy(vSync = it) } }
        ToggleRow(stringResource(R.string.settings_core_stretch_display), config.stretchDisplayArea, stringResource(R.string.settings_help_stretch_display), { onUpdate { it.copy(stretchDisplayArea = defaults.stretchDisplayArea) } }) { onUpdate { cfg -> cfg.copy(stretchDisplayArea = it) } }
        ToggleRow(stringResource(R.string.settings_core_fps_hack), config.fpsHack, stringResource(R.string.settings_help_fps_hack), { onUpdate { it.copy(fpsHack = defaults.fpsHack) } }) { onUpdate { cfg -> cfg.copy(fpsHack = it) } }
        val unlimitedLabel = stringResource(R.string.settings_frame_limit_unlimited)
        val frameLimitOptions = FrameLimit.supportedValues.map { limit ->
            if (limit == FrameLimit.UNLIMITED) unlimitedLabel else "$limit FPS"
        }
        ChoiceRow(
            stringResource(R.string.settings_frame_limit),
            stringResource(R.string.settings_help_frame_limit),
            if (config.frameLimit == FrameLimit.UNLIMITED) unlimitedLabel else "${config.frameLimit} FPS",
            frameLimitOptions,
            { onUpdate { it.copy(frameLimit = defaults.frameLimit) } }
        ) { selected ->
            val selectedIndex = frameLimitOptions.indexOf(selected)
            val limit = FrameLimit.supportedValues.getOrElse(selectedIndex) { FrameLimit.UNLIMITED }
            onUpdate { cfg -> cfg.copy(frameLimit = limit) }
        }
        ToggleRow(stringResource(R.string.settings_core_disable_surface_sync), config.disableSurfaceSync, stringResource(R.string.settings_help_disable_surface_sync), { onUpdate { it.copy(disableSurfaceSync = defaults.disableSurfaceSync) } }) { onUpdate { cfg -> cfg.copy(disableSurfaceSync = it) } }
        ToggleRow(stringResource(R.string.settings_core_texture_cache), config.textureCache, stringResource(R.string.settings_help_texture_cache), { onUpdate { it.copy(textureCache = defaults.textureCache) } }) { onUpdate { cfg -> cfg.copy(textureCache = it) } }
        ToggleRow(stringResource(R.string.settings_core_async_pipeline), config.asyncPipelineCompilation, stringResource(R.string.settings_help_async_pipeline), { onUpdate { it.copy(asyncPipelineCompilation = defaults.asyncPipelineCompilation) } }) { onUpdate { cfg -> cfg.copy(asyncPipelineCompilation = it) } }
        ToggleRow(stringResource(R.string.settings_shader_cache), config.shaderCache, stringResource(R.string.settings_help_shader_cache), { onUpdate { it.copy(shaderCache = defaults.shaderCache) } }) { onUpdate { cfg -> cfg.copy(shaderCache = it) } }
        ToggleRow(stringResource(R.string.settings_core_shader_compilation_notice), config.showCompileShaders, stringResource(R.string.settings_help_shader_compilation_notice), { onUpdate { it.copy(showCompileShaders = defaults.showCompileShaders) } }) { onUpdate { cfg -> cfg.copy(showCompileShaders = it) } }
    }
}

@Composable
private fun GpuDriverChoiceRow(
    effectiveDriverName: String,
    globalDriverName: String,
    customDriverOverride: String?,
    backendRenderer: String,
    installedGpuDrivers: List<InstalledGpuDriver>,
    onOpenGpuDriverManager: () -> Unit,
    onReset: () -> Unit,
    onSelected: (String?) -> Unit
) {
    val selectedDriver = installedGpuDrivers.firstOrNull { it.name == effectiveDriverName }
    val status = when {
        customDriverOverride == null && globalDriverName.isBlank() -> stringResource(R.string.settings_gpu_driver_status_global_system)
        customDriverOverride == null -> stringResource(R.string.settings_gpu_driver_status_global, globalDriverName)
        customDriverOverride.isBlank() -> stringResource(R.string.settings_gpu_driver_status_game_system)
        selectedDriver == null -> stringResource(R.string.settings_gpu_driver_status_broken, effectiveDriverName)
        !selectedDriver.isUsable -> stringResource(R.string.settings_gpu_driver_status_broken, selectedDriver.name)
        backendRenderer != "Vulkan" -> stringResource(R.string.settings_gpu_driver_status_renderer, backendRenderer)
        else -> stringResource(R.string.settings_gpu_driver_status_game_active, selectedDriver.name)
    }

    SettingContainer(
        title = stringResource(R.string.settings_gpu_driver),
        description = stringResource(R.string.settings_help_gpu_driver),
        onReset = onReset
    ) {
        Text(
            text = status,
            style = MaterialTheme.typography.bodySmall,
            color = MaterialTheme.colorScheme.onSurfaceVariant
        )
        val selectedLabel = when {
            customDriverOverride == null -> stringResource(R.string.settings_gpu_driver_global)
            customDriverOverride.isBlank() -> stringResource(R.string.settings_gpu_driver_system)
            else -> customDriverOverride
        }
        FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
            FilterChip(
                selected = true,
                onClick = onOpenGpuDriverManager,
                colors = FilterChipDefaults.filterChipColors(
                    selectedContainerColor = MaterialTheme.colorScheme.secondaryContainer,
                    selectedLabelColor = MaterialTheme.colorScheme.onSecondaryContainer
                ),
                label = {
                    Text(
                        text = selectedLabel,
                        maxLines = 1,
                        overflow = TextOverflow.Ellipsis
                    )
                }
            )
        }
        if (installedGpuDrivers.isEmpty()) {
            Text(
                text = stringResource(R.string.settings_gpu_driver_none_installed),
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
        }
        OutlinedButton(
            onClick = onOpenGpuDriverManager,
            modifier = Modifier.fillMaxWidth()
        ) {
            Text(stringResource(R.string.settings_gpu_driver_manage))
        }
    }
}

@Composable
private fun PerformanceProfileSection(
    config: VitaCoreConfig,
    defaults: VitaCoreConfig,
    onUpdate: ((VitaCoreConfig) -> VitaCoreConfig) -> Unit
) {
    SectionCard(title = stringResource(R.string.game_manager_system_performance)) {
        ChoiceRow(stringResource(R.string.settings_modules_mode), stringResource(R.string.settings_help_modules_mode), config.modulesMode.toString(), listOf("0", "1", "2"), { onUpdate { it.copy(modulesMode = defaults.modulesMode) } }) {
            onUpdate { cfg -> cfg.copy(modulesMode = it.toIntOrNull() ?: defaults.modulesMode) }
        }
        SliderRow(
            title = stringResource(R.string.settings_cpu_pool_size),
            description = stringResource(R.string.settings_help_cpu_pool_size),
            valueText = stringResource(R.string.settings_cpu_pool_size_value, config.cpuPoolSize),
            value = config.cpuPoolSize.toFloat(),
            valueRange = 1f..32f,
            steps = 30,
            onReset = { onUpdate { it.copy(cpuPoolSize = defaults.cpuPoolSize) } },
            onChange = { value -> onUpdate { it.copy(cpuPoolSize = value.toInt().coerceIn(1, 32)) } }
        )
        ToggleRow(stringResource(R.string.settings_core_pstv_mode), config.pstvMode, stringResource(R.string.settings_help_pstv_mode), { onUpdate { it.copy(pstvMode = defaults.pstvMode) } }) { onUpdate { cfg -> cfg.copy(pstvMode = it) } }
        ToggleRow(stringResource(R.string.game_manager_psn_signed_in), config.psnSignedIn, stringResource(R.string.settings_help_psn_signed_in), { onUpdate { it.copy(psnSignedIn = defaults.psnSignedIn) } }) { onUpdate { cfg -> cfg.copy(psnSignedIn = it) } }
        ToggleRow(stringResource(R.string.settings_show_info_bar), config.showInfoBar, stringResource(R.string.settings_help_show_info_bar), { onUpdate { it.copy(showInfoBar = defaults.showInfoBar) } }) { onUpdate { cfg -> cfg.copy(showInfoBar = it) } }
        ToggleRow(stringResource(R.string.settings_core_shader_caching_warn), config.showShaderCacheWarn, stringResource(R.string.settings_help_shader_caching_warn), { onUpdate { it.copy(showShaderCacheWarn = defaults.showShaderCacheWarn) } }) { onUpdate { cfg -> cfg.copy(showShaderCacheWarn = it) } }
    }
}

@Composable
private fun AudioProfileSection(
    config: VitaCoreConfig,
    defaults: VitaCoreConfig,
    onUpdate: ((VitaCoreConfig) -> VitaCoreConfig) -> Unit
) {
    SectionCard(title = stringResource(R.string.settings_core_audio_title)) {
        ChoiceRow(stringResource(R.string.settings_core_audio_backend_label), stringResource(R.string.settings_help_audio_backend), config.audioBackend, listOf("SDL", "Cubeb"), { onUpdate { it.copy(audioBackend = defaults.audioBackend) } }) {
            onUpdate { cfg -> cfg.copy(audioBackend = it) }
        }
        SliderRow(
            title = stringResource(R.string.settings_core_audio_volume_label),
            description = stringResource(R.string.settings_help_audio_volume),
            valueText = stringResource(R.string.settings_core_audio_volume_value, config.audioVolume),
            value = config.audioVolume.toFloat(),
            valueRange = 0f..100f,
            onReset = { onUpdate { it.copy(audioVolume = defaults.audioVolume) } },
            onChange = { value -> onUpdate { it.copy(audioVolume = value.toInt()) } }
        )
        SliderRow(
            title = stringResource(R.string.settings_bgm_volume),
            description = stringResource(R.string.settings_help_bgm_volume),
            valueText = stringResource(R.string.settings_bgm_volume_value, config.bgmVolume),
            value = config.bgmVolume.toFloat(),
            valueRange = 0f..100f,
            onReset = { onUpdate { it.copy(bgmVolume = defaults.bgmVolume) } },
            onChange = { value -> onUpdate { it.copy(bgmVolume = value.toInt()) } }
        )
        ToggleRow(stringResource(R.string.settings_core_ngs_enable), config.ngsEnable, stringResource(R.string.settings_help_ngs_enable), { onUpdate { it.copy(ngsEnable = defaults.ngsEnable) } }) { onUpdate { cfg -> cfg.copy(ngsEnable = it) } }
    }
}

@Composable
private fun OverlayProfileSection(
    config: VitaCoreConfig,
    defaults: VitaCoreConfig,
    onUpdate: ((VitaCoreConfig) -> VitaCoreConfig) -> Unit
) {
    SectionCard(title = stringResource(R.string.settings_tab_overlay)) {
        ToggleRow(stringResource(R.string.settings_core_performance_overlay), config.performanceOverlay, stringResource(R.string.settings_help_performance_overlay), { onUpdate { it.copy(performanceOverlay = defaults.performanceOverlay) } }) { onUpdate { cfg -> cfg.copy(performanceOverlay = it) } }
        ChoiceRow(stringResource(R.string.settings_core_overlay_detail_title), stringResource(R.string.settings_help_performance_overlay_detail), config.performanceOverlayDetail.toString(), listOf("0", "1", "2", "3"), { onUpdate { it.copy(performanceOverlayDetail = defaults.performanceOverlayDetail) } }) {
            onUpdate { cfg -> cfg.copy(performanceOverlayDetail = it.toIntOrNull() ?: defaults.performanceOverlayDetail) }
        }
        ChoiceRow(stringResource(R.string.settings_core_overlay_position_title), stringResource(R.string.settings_help_performance_overlay_position), config.performanceOverlayPosition.toString(), listOf("0", "1", "2", "3", "4", "5"), { onUpdate { it.copy(performanceOverlayPosition = defaults.performanceOverlayPosition) } }) {
            onUpdate { cfg -> cfg.copy(performanceOverlayPosition = it.toIntOrNull() ?: defaults.performanceOverlayPosition) }
        }
        ToggleRow(stringResource(R.string.settings_core_gamepad_overlay), config.enableGamepadOverlay, stringResource(R.string.settings_help_gamepad_overlay), { onUpdate { it.copy(enableGamepadOverlay = defaults.enableGamepadOverlay) } }) { onUpdate { cfg -> cfg.copy(enableGamepadOverlay = it) } }
        ToggleRow(stringResource(R.string.settings_core_touchpad_cursor), config.showTouchpadCursor, stringResource(R.string.settings_help_touchpad_cursor), { onUpdate { it.copy(showTouchpadCursor = defaults.showTouchpadCursor) } }) { onUpdate { cfg -> cfg.copy(showTouchpadCursor = it) } }
        SliderRow(
            title = stringResource(R.string.settings_core_overlay_scale_label),
            description = stringResource(R.string.settings_help_overlay_scale),
            valueText = stringResource(R.string.settings_core_overlay_scale_value, config.overlayScale),
            value = config.overlayScale,
            valueRange = 0.6f..1.4f,
            steps = 7,
            onReset = { onUpdate { it.copy(overlayScale = defaults.overlayScale) } },
            onChange = { value -> onUpdate { it.copy(overlayScale = (value * 10).toInt() / 10f) } }
        )
        SliderRow(
            title = stringResource(R.string.settings_core_overlay_opacity_label),
            description = stringResource(R.string.settings_help_overlay_opacity),
            valueText = stringResource(R.string.settings_core_overlay_opacity_value, config.overlayOpacity),
            value = config.overlayOpacity.toFloat(),
            valueRange = 10f..100f,
            steps = 8,
            onReset = { onUpdate { it.copy(overlayOpacity = defaults.overlayOpacity) } },
            onChange = { value -> onUpdate { it.copy(overlayOpacity = value.toInt()) } }
        )
    }
}

@Composable
private fun GamepadProfileSection(
    config: VitaCoreConfig,
    defaults: VitaCoreConfig,
    onUpdate: ((VitaCoreConfig) -> VitaCoreConfig) -> Unit
) {
    SectionCard(title = stringResource(R.string.settings_touch_controls_section)) {
        ToggleRow(
            stringResource(R.string.settings_touch_haptics),
            config.touchHaptics,
            stringResource(R.string.settings_help_touch_haptics),
            { onUpdate { it.copy(touchHaptics = defaults.touchHaptics) } }
        ) { enabled -> onUpdate { it.copy(touchHaptics = enabled) } }
        IntChoiceRow(
            title = stringResource(R.string.settings_touch_haptics_preset),
            description = stringResource(R.string.settings_help_touch_haptics_preset),
            selected = config.touchHapticsPreset,
            options = listOf(
                VitaCoreConfig.TOUCH_HAPTICS_PRESET_SOFT to stringResource(R.string.settings_touch_haptics_preset_soft),
                VitaCoreConfig.TOUCH_HAPTICS_PRESET_BALANCED to stringResource(R.string.settings_touch_haptics_preset_balanced),
                VitaCoreConfig.TOUCH_HAPTICS_PRESET_CRISP to stringResource(R.string.settings_touch_haptics_preset_crisp),
                VitaCoreConfig.TOUCH_HAPTICS_PRESET_STRONG to stringResource(R.string.settings_touch_haptics_preset_strong)
            ),
            enabled = config.touchHaptics,
            onReset = { onUpdate { it.copy(touchHapticsPreset = defaults.touchHapticsPreset) } }
        ) { value -> onUpdate { it.copy(touchHapticsPreset = value) } }
        SliderRow(
            title = stringResource(R.string.settings_touch_haptics_strength),
            description = stringResource(R.string.settings_help_touch_haptics_strength),
            valueText = stringResource(R.string.settings_gamepad_percent_value, config.touchHapticsStrength),
            value = config.touchHapticsStrength.toFloat(),
            valueRange = 10f..100f,
            steps = 17,
            enabled = config.touchHaptics,
            onReset = { onUpdate { it.copy(touchHapticsStrength = defaults.touchHapticsStrength) } },
            onChange = { value -> onUpdate { it.copy(touchHapticsStrength = value.roundToInt()) } }
        )
    }
    SectionCard(title = stringResource(R.string.settings_gyro_mode)) {
        IntChoiceRow(
            title = stringResource(R.string.settings_gyro_mode),
            description = stringResource(R.string.settings_help_gyro_mode),
            selected = config.gyroMode,
            options = listOf(
                VitaCoreConfig.GYRO_MODE_OFF to stringResource(R.string.settings_gyro_off),
                VitaCoreConfig.GYRO_MODE_AIM to stringResource(R.string.settings_gyro_aim),
                VitaCoreConfig.GYRO_MODE_STEERING to stringResource(R.string.settings_gyro_steering)
            ),
            onReset = { onUpdate { it.copy(gyroMode = defaults.gyroMode) } }
        ) { value -> onUpdate { it.copy(gyroMode = value) } }
        if (config.gyroMode != VitaCoreConfig.GYRO_MODE_OFF) {
            SliderRow(
                title = stringResource(R.string.settings_gyro_sensitivity),
                description = stringResource(R.string.settings_help_gyro_sensitivity),
                valueText = stringResource(R.string.settings_gamepad_percent_value, config.gyroSensitivity),
                value = config.gyroSensitivity.toFloat(),
                valueRange = 25f..300f,
                steps = 10,
                onReset = { onUpdate { it.copy(gyroSensitivity = defaults.gyroSensitivity) } },
                onChange = { value -> onUpdate { it.copy(gyroSensitivity = value.roundToInt()) } }
            )
            SliderRow(
                title = stringResource(R.string.settings_gyro_smoothing),
                description = stringResource(R.string.settings_help_gyro_smoothing),
                valueText = stringResource(R.string.settings_gamepad_percent_value, config.gyroSmoothing),
                value = config.gyroSmoothing.toFloat(),
                valueRange = 0f..90f,
                steps = 8,
                onReset = { onUpdate { it.copy(gyroSmoothing = defaults.gyroSmoothing) } },
                onChange = { value -> onUpdate { it.copy(gyroSmoothing = value.roundToInt()) } }
            )
            ToggleRow(stringResource(R.string.settings_gyro_invert_x), config.gyroInvertX, stringResource(R.string.settings_gyro_invert_x_desc), { onUpdate { it.copy(gyroInvertX = defaults.gyroInvertX) } }) { enabled -> onUpdate { it.copy(gyroInvertX = enabled) } }
            if (config.gyroMode == VitaCoreConfig.GYRO_MODE_AIM) {
                ToggleRow(stringResource(R.string.settings_gyro_invert_y), config.gyroInvertY, stringResource(R.string.settings_gyro_invert_y_desc), { onUpdate { it.copy(gyroInvertY = defaults.gyroInvertY) } }) { enabled -> onUpdate { it.copy(gyroInvertY = enabled) } }
            }
        }
    }
    SectionCard(title = stringResource(R.string.settings_gamepad_section)) {
        SliderRow(
            title = stringResource(R.string.settings_gamepad_deadzone),
            description = stringResource(R.string.settings_help_gamepad_deadzone),
            valueText = stringResource(R.string.settings_gamepad_percent_value, (config.gamepadDeadzone * 100).toInt()),
            value = config.gamepadDeadzone,
            valueRange = 0f..0.4f,
            steps = 7,
            onReset = { onUpdate { it.copy(gamepadDeadzone = defaults.gamepadDeadzone) } },
            onChange = { value -> onUpdate { it.copy(gamepadDeadzone = value) } }
        )
        SliderRow(
            title = stringResource(R.string.settings_core_analog_multiplier_label),
            description = stringResource(R.string.settings_help_analog_multiplier),
            valueText = stringResource(R.string.settings_core_analog_multiplier_value, config.analogMultiplier),
            value = config.analogMultiplier,
            valueRange = 0.5f..2f,
            steps = 5,
            onReset = { onUpdate { it.copy(analogMultiplier = defaults.analogMultiplier) } },
            onChange = { value -> onUpdate { it.copy(analogMultiplier = (value * 10).toInt() / 10f) } }
        )
        SliderRow(
            title = stringResource(R.string.settings_gamepad_trigger_threshold),
            description = stringResource(R.string.settings_help_gamepad_trigger_threshold),
            valueText = stringResource(R.string.settings_gamepad_percent_value, (config.gamepadTriggerThreshold * 100).toInt()),
            value = config.gamepadTriggerThreshold,
            valueRange = 0f..0.5f,
            steps = 9,
            onReset = { onUpdate { it.copy(gamepadTriggerThreshold = defaults.gamepadTriggerThreshold) } },
            onChange = { value -> onUpdate { it.copy(gamepadTriggerThreshold = value) } }
        )
        ChoiceRow(
            title = stringResource(R.string.settings_gamepad_button_profile),
            description = stringResource(R.string.settings_help_gamepad_button_profile),
            selected = config.gamepadButtonProfile,
            options = listOf(
                VitaCoreConfig.GAMEPAD_PROFILE_STANDARD,
                VitaCoreConfig.GAMEPAD_PROFILE_SWAP_CROSS_CIRCLE,
                VitaCoreConfig.GAMEPAD_PROFILE_NINTENDO_FACE
            ),
            onReset = { onUpdate { it.copy(gamepadButtonProfile = defaults.gamepadButtonProfile) } }
        ) {
            onUpdate { cfg -> cfg.copy(gamepadButtonProfile = it) }
        }
        ToggleRow(stringResource(R.string.settings_vibration_enable), config.gamepadVibration, stringResource(R.string.settings_help_vibration_enable), { onUpdate { it.copy(gamepadVibration = defaults.gamepadVibration) } }) { onUpdate { cfg -> cfg.copy(gamepadVibration = it) } }
        SliderRow(
            title = stringResource(R.string.settings_vibration_strength),
            description = stringResource(R.string.settings_help_vibration_strength),
            valueText = stringResource(R.string.settings_gamepad_percent_value, config.gamepadVibrationStrength),
            value = config.gamepadVibrationStrength.toFloat(),
            valueRange = 0f..100f,
            steps = 19,
            onReset = { onUpdate { it.copy(gamepadVibrationStrength = defaults.gamepadVibrationStrength) } },
            onChange = { value -> onUpdate { it.copy(gamepadVibrationStrength = value.toInt().coerceIn(0, 100)) } }
        )
        ToggleRow(stringResource(R.string.settings_device_vibration_fallback), config.deviceVibrationFallback, stringResource(R.string.settings_help_device_vibration_fallback), { onUpdate { it.copy(deviceVibrationFallback = defaults.deviceVibrationFallback) } }) { onUpdate { cfg -> cfg.copy(deviceVibrationFallback = it) } }
        ToggleRow(stringResource(R.string.settings_gamepad_swap_sticks), config.gamepadSwapSticks, stringResource(R.string.settings_help_gamepad_swap_sticks), { onUpdate { it.copy(gamepadSwapSticks = defaults.gamepadSwapSticks) } }) { onUpdate { cfg -> cfg.copy(gamepadSwapSticks = it) } }
        ToggleRow(stringResource(R.string.game_manager_invert_left_x), config.gamepadInvertLeftX, stringResource(R.string.settings_help_gamepad_invert_y), { onUpdate { it.copy(gamepadInvertLeftX = defaults.gamepadInvertLeftX) } }) { onUpdate { cfg -> cfg.copy(gamepadInvertLeftX = it) } }
        ToggleRow(stringResource(R.string.settings_gamepad_invert_left_y), config.gamepadInvertLeftY, stringResource(R.string.settings_help_gamepad_invert_y), { onUpdate { it.copy(gamepadInvertLeftY = defaults.gamepadInvertLeftY) } }) { onUpdate { cfg -> cfg.copy(gamepadInvertLeftY = it) } }
        ToggleRow(stringResource(R.string.game_manager_invert_right_x), config.gamepadInvertRightX, stringResource(R.string.settings_help_gamepad_invert_y), { onUpdate { it.copy(gamepadInvertRightX = defaults.gamepadInvertRightX) } }) { onUpdate { cfg -> cfg.copy(gamepadInvertRightX = it) } }
        ToggleRow(stringResource(R.string.settings_gamepad_invert_right_y), config.gamepadInvertRightY, stringResource(R.string.settings_help_gamepad_invert_y), { onUpdate { it.copy(gamepadInvertRightY = defaults.gamepadInvertRightY) } }) { onUpdate { cfg -> cfg.copy(gamepadInvertRightY = it) } }
    }
}

@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun ChoiceRow(
    title: String,
    description: String = title,
    selected: String,
    options: List<String>,
    onReset: (() -> Unit)? = null,
    onSelected: (String) -> Unit
) {
    SettingContainer(title = title, description = description, onReset = onReset) {
        FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
            options.forEach { option ->
                FilterChip(
                    selected = selected == option,
                    onClick = { onSelected(option) },
                    colors = FilterChipDefaults.filterChipColors(
                        selectedContainerColor = MaterialTheme.colorScheme.secondaryContainer,
                        selectedLabelColor = MaterialTheme.colorScheme.onSecondaryContainer
                    ),
                    label = { Text(option) }
                )
            }
        }
    }
}

@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun IntChoiceRow(
    title: String,
    description: String = title,
    selected: Int,
    options: List<Pair<Int, String>>,
    enabled: Boolean = true,
    onReset: (() -> Unit)? = null,
    onSelected: (Int) -> Unit
) {
    SettingContainer(
        title = title,
        description = description,
        onReset = onReset,
        enabled = enabled
    ) {
        FlowRow(
            horizontalArrangement = Arrangement.spacedBy(8.dp),
            verticalArrangement = Arrangement.spacedBy(8.dp)
        ) {
            options.forEach { (value, label) ->
                FilterChip(
                    selected = selected == value,
                    enabled = enabled,
                    onClick = { onSelected(value) },
                    colors = FilterChipDefaults.filterChipColors(
                        selectedContainerColor = MaterialTheme.colorScheme.secondaryContainer,
                        selectedLabelColor = MaterialTheme.colorScheme.onSecondaryContainer
                    ),
                    label = { Text(label) }
                )
            }
        }
    }
}

@Composable
private fun ToggleRow(
    title: String,
    checked: Boolean,
    description: String = title,
    onReset: (() -> Unit)? = null,
    enabled: Boolean = true,
    onCheckedChange: (Boolean) -> Unit
) {
    SettingContainer(title = title, description = description, onReset = onReset, enabled = enabled) {
        Row(
            modifier = Modifier.fillMaxWidth(),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.End
        ) {
            Switch(checked = checked, onCheckedChange = onCheckedChange, enabled = enabled)
        }
    }
}

@Composable
private fun SliderRow(
    title: String,
    description: String = title,
    valueText: String,
    value: Float,
    valueRange: ClosedFloatingPointRange<Float>,
    steps: Int = 0,
    enabled: Boolean = true,
    onReset: () -> Unit,
    onChange: (Float) -> Unit
) {
    SettingContainer(title = title, description = description, onReset = onReset, enabled = enabled) {
        Text(
            text = valueText,
            style = MaterialTheme.typography.bodySmall,
            color = MaterialTheme.colorScheme.onSurfaceVariant
        )
        Slider(
            enabled = enabled,
            value = value,
            onValueChange = onChange,
            valueRange = valueRange,
            steps = steps
        )
    }
}

@Composable
private fun SettingContainer(
    title: String,
    description: String,
    onReset: (() -> Unit)? = null,
    enabled: Boolean = true,
    content: @Composable ColumnScope.() -> Unit
) {
    Surface(
        modifier = Modifier
            .fillMaxWidth()
            .combinedClickable(
                onClick = {},
                onLongClick = { onReset?.invoke() }
            ),
        shape = RoundedCornerShape(18.dp),
        color = MaterialTheme.colorScheme.surfaceContainerHigh,
        tonalElevation = 1.dp,
        border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.55f))
    ) {
        Column(
            modifier = Modifier.padding(CardContentPadding),
            verticalArrangement = Arrangement.spacedBy(10.dp),
        ) {
            Row(
                modifier = Modifier.fillMaxWidth(),
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(10.dp)
            ) {
                Text(
                    text = title,
                    modifier = Modifier.weight(1f),
                    style = MaterialTheme.typography.bodyLarge.copy(fontWeight = FontWeight.SemiBold),
                    color = if (enabled) MaterialTheme.colorScheme.onSurface else MaterialTheme.colorScheme.onSurface.copy(alpha = 0.5f)
                )
                SettingHelpButton(title = title, description = description)
            }
            content()
        }
    }
}

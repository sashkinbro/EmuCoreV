@file:OptIn(androidx.compose.foundation.layout.ExperimentalLayoutApi::class)

package com.sbro.emucorev.ui.emulation

import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.rounded.ExitToApp
import androidx.compose.material.icons.rounded.CameraAlt
import androidx.compose.material.icons.rounded.CheckCircle
import androidx.compose.material.icons.rounded.Edit
import androidx.compose.material.icons.rounded.EmojiEvents
import androidx.compose.material.icons.rounded.Lock
import androidx.compose.material.icons.rounded.Pause
import androidx.compose.material.icons.rounded.PlayArrow
import androidx.compose.material.icons.rounded.Refresh
import androidx.compose.material.icons.rounded.Settings
import androidx.compose.material.icons.rounded.SportsEsports
import androidx.compose.material.icons.rounded.Tune
import androidx.compose.material.icons.rounded.Visibility
import androidx.compose.material.icons.rounded.VisibilityOff
import androidx.compose.material3.FilterChip
import androidx.compose.material3.Icon
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Slider
import androidx.compose.material3.Surface
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.luminance
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.layout.layout
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import com.sbro.emucorev.R
import com.sbro.emucorev.core.VitaCoreConfig
import com.sbro.emucorev.data.TrophyRepository
import com.sbro.emucorev.data.GameMenuLayoutStyle
import com.sbro.emucorev.data.VitaTrophy
import com.sbro.emucorev.data.VitaTrophyGrade
import com.sbro.emucorev.data.VitaTrophySet
import com.sbro.emucorev.ui.common.LocalImage
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.withContext
import kotlin.math.roundToInt

private val LiveBadgeColor = Color(0xFF34D27A)
private val RestartBadgeColor = Color(0xFFE0A82E)

private data class EmulationMenuPalette(
    val panel: Color,
    val panelSoft: Color,
    val row: Color,
    val border: Color,
    val textPrimary: Color,
    val textSecondary: Color
)

@Composable
private fun emulationMenuPalette(): EmulationMenuPalette {
    val scheme = MaterialTheme.colorScheme
    val dark = scheme.background.luminance() < 0.5f
    return EmulationMenuPalette(
        panel = if (dark) Color(0xEE10131A) else Color(0xF7FAFBFF),
        panelSoft = if (dark) Color(0xF01A1F2A) else Color(0xFFFFFFFF),
        row = if (dark) Color.White.copy(alpha = 0.075f) else Color(0xFFEEF2F8),
        border = if (dark) Color.White.copy(alpha = 0.12f) else Color(0xFFD6DDE8),
        textPrimary = if (dark) Color.White else scheme.onSurface,
        textSecondary = if (dark) Color(0xFFB8C0CC) else scheme.onSurfaceVariant
    )
}

/**
 * Floating toolbar shown above the game when the user invokes the pause UI.
 * Stays compact so it doesn't cover important on-screen UI.
 */
@Composable
fun EmulationQuickBar(
    paused: Boolean,
    onPauseToggle: () -> Unit,
    onScreenshot: () -> Unit,
    onOpenMenu: () -> Unit,
    modifier: Modifier = Modifier
) {
    val palette = emulationMenuPalette()
    Surface(
        modifier = modifier.padding(top = 12.dp),
        shape = RoundedCornerShape(28.dp),
        color = palette.panel,
        border = BorderStroke(1.dp, palette.border),
        tonalElevation = 4.dp,
        shadowElevation = 12.dp
    ) {
        Row(
            modifier = Modifier.padding(horizontal = 8.dp, vertical = 6.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(4.dp)
        ) {
            QuickBarButton(
                icon = if (paused) Icons.Rounded.PlayArrow else Icons.Rounded.Pause,
                contentDescription = stringResource(
                    if (paused) R.string.emulation_resume else R.string.emulation_pause
                ),
                onClick = onPauseToggle,
                palette = palette
            )
            QuickBarButton(
                icon = Icons.Rounded.CameraAlt,
                contentDescription = stringResource(R.string.emulation_quickbar_screenshot),
                onClick = onScreenshot,
                palette = palette
            )
            QuickBarButton(
                icon = Icons.Rounded.Tune,
                contentDescription = stringResource(R.string.emulation_quickbar_open_menu),
                onClick = onOpenMenu,
                palette = palette
            )
        }
    }
}

@Composable
private fun QuickBarButton(
    icon: ImageVector,
    contentDescription: String,
    onClick: () -> Unit,
    palette: EmulationMenuPalette
) {
    Box(
        modifier = Modifier
            .size(48.dp)
            .clip(CircleShape)
            .background(palette.row)
            .clickable(onClick = onClick),
        contentAlignment = Alignment.Center
    ) {
        Icon(
            imageVector = icon,
            contentDescription = contentDescription,
            tint = palette.textPrimary,
            modifier = Modifier.size(22.dp)
        )
    }
}
@Composable
fun EmulationGameMenu(
    gameTitle: String,
    gameId: String,
    config: VitaCoreConfig,
    paused: Boolean,
    sessionElapsedMs: Long,
    expandHorizontally: Boolean,
    layoutStyle: GameMenuLayoutStyle,
    physicalGamepadConnected: Boolean,
    callbacks: EmulationMenuCallbacks,
    modifier: Modifier = Modifier
) {
    val palette = emulationMenuPalette()
    var selectedTab by remember { mutableStateOf(EmulationMenuTab.Game) }
    val effectiveStyle = if (expandHorizontally) layoutStyle else GameMenuLayoutStyle.COMMAND_CENTER
    val shape = if (!expandHorizontally) {
        RoundedCornerShape(topStart = 24.dp, topEnd = 24.dp)
    } else {
        when (effectiveStyle) {
            GameMenuLayoutStyle.DASHBOARD,
            GameMenuLayoutStyle.COMMAND_CENTER -> RoundedCornerShape(24.dp)
            GameMenuLayoutStyle.COMPACT -> RoundedCornerShape(topStart = 16.dp, bottomStart = 16.dp)
            GameMenuLayoutStyle.SIDEBAR -> RoundedCornerShape(topStart = 24.dp, bottomStart = 24.dp)
        }
    }
    Surface(
        modifier = modifier
            .padding(vertical = 4.dp)
            .then(
                if (expandHorizontally) {
                    when (effectiveStyle) {
                        GameMenuLayoutStyle.DASHBOARD -> Modifier
                            .fillMaxHeight()
                            .fillMaxWidth(0.86f)
                            .widthIn(max = 980.dp)
                        GameMenuLayoutStyle.COMMAND_CENTER -> Modifier
                            .fillMaxHeight()
                            .fillMaxWidth(0.92f)
                            .widthIn(max = 1120.dp)
                        GameMenuLayoutStyle.COMPACT -> Modifier
                            .fillMaxHeight()
                            .widthIn(min = 330.dp, max = 390.dp)
                        GameMenuLayoutStyle.SIDEBAR -> Modifier
                            .fillMaxHeight()
                            .widthIn(min = 470.dp, max = 560.dp)
                    }
                } else {
                    Modifier
                        .fillMaxWidth()
                        .fillMaxHeight(0.9f)
                        .heightIn(min = 460.dp)
                }
            ),
        shape = shape,
        color = palette.panel,
        border = BorderStroke(1.dp, palette.border),
        tonalElevation = 6.dp,
        shadowElevation = 18.dp
    ) {
        when (effectiveStyle) {
            GameMenuLayoutStyle.DASHBOARD -> Row(modifier = Modifier.fillMaxSize()) {
                MenuVerticalTabs(
                    selectedTab = selectedTab,
                    onSelected = { selectedTab = it },
                    iconOnly = false,
                    modifier = Modifier
                        .width(188.dp)
                        .fillMaxHeight()
                        .padding(14.dp)
                )
                MenuScrollableContent(
                    gameTitle = gameTitle,
                    gameId = gameId,
                    config = config,
                    paused = paused,
                    sessionElapsedMs = sessionElapsedMs,
                    physicalGamepadConnected = physicalGamepadConnected,
                    callbacks = callbacks,
                    selectedTab = selectedTab,
                    showHorizontalTabs = false,
                    horizontalPadding = 18.dp,
                    modifier = Modifier.weight(1f)
                )
            }
            GameMenuLayoutStyle.SIDEBAR -> Row(modifier = Modifier.fillMaxSize()) {
                MenuScrollableContent(
                    gameTitle = gameTitle,
                    gameId = gameId,
                    config = config,
                    paused = paused,
                    sessionElapsedMs = sessionElapsedMs,
                    physicalGamepadConnected = physicalGamepadConnected,
                    callbacks = callbacks,
                    selectedTab = selectedTab,
                    showHorizontalTabs = false,
                    horizontalPadding = 18.dp,
                    modifier = Modifier.weight(1f)
                )
                MenuVerticalTabs(
                    selectedTab = selectedTab,
                    onSelected = { selectedTab = it },
                    iconOnly = true,
                    modifier = Modifier
                        .width(66.dp)
                        .fillMaxHeight()
                        .padding(vertical = 14.dp, horizontal = 8.dp)
                )
            }
            GameMenuLayoutStyle.COMMAND_CENTER,
            GameMenuLayoutStyle.COMPACT -> MenuScrollableContent(
                gameTitle = gameTitle,
                gameId = gameId,
                config = config,
                paused = paused,
                sessionElapsedMs = sessionElapsedMs,
                physicalGamepadConnected = physicalGamepadConnected,
                callbacks = callbacks,
                selectedTab = selectedTab,
                showHorizontalTabs = true,
                horizontalPadding = if (effectiveStyle == GameMenuLayoutStyle.COMPACT) 12.dp else 18.dp,
                compact = effectiveStyle == GameMenuLayoutStyle.COMPACT,
                showSheetHandle = !expandHorizontally,
                onSelected = { selectedTab = it }
            )
        }
    }
}

@Composable
private fun MenuScrollableContent(
    gameTitle: String,
    gameId: String,
    config: VitaCoreConfig,
    paused: Boolean,
    sessionElapsedMs: Long,
    physicalGamepadConnected: Boolean,
    callbacks: EmulationMenuCallbacks,
    selectedTab: EmulationMenuTab,
    showHorizontalTabs: Boolean,
    horizontalPadding: Dp,
    modifier: Modifier = Modifier,
    compact: Boolean = false,
    showSheetHandle: Boolean = false,
    onSelected: (EmulationMenuTab) -> Unit = {}
) {
    Column(
        modifier = modifier
            .fillMaxSize()
            .verticalScroll(rememberScrollState())
            .padding(
                start = horizontalPadding,
                end = horizontalPadding,
                top = if (compact) 10.dp else 14.dp,
                bottom = 16.dp
            ),
        verticalArrangement = Arrangement.spacedBy(if (compact) 9.dp else 12.dp)
    ) {
        if (showSheetHandle) SheetHandle()
        MenuHeader(gameTitle = gameTitle, gameId = gameId, paused = paused)
        MenuTopActions(paused = paused, callbacks = callbacks)
        if (showHorizontalTabs) {
            Column(
                modifier = Modifier
                    .fillMaxWidth()
                    .horizontalBleed(horizontalPadding)
            ) {
                MenuTabs(
                    selectedTab = selectedTab,
                    onSelected = onSelected,
                    horizontalContentPadding = horizontalPadding
                )
            }
        }
        MenuSelectedContent(
            selectedTab = selectedTab,
            gameId = gameId,
            config = config,
            sessionElapsedMs = sessionElapsedMs,
            physicalGamepadConnected = physicalGamepadConnected,
            callbacks = callbacks
        )
    }
}

@Composable
private fun MenuSelectedContent(
    selectedTab: EmulationMenuTab,
    gameId: String,
    config: VitaCoreConfig,
    sessionElapsedMs: Long,
    physicalGamepadConnected: Boolean,
    callbacks: EmulationMenuCallbacks
) {
    Column(
        modifier = Modifier.fillMaxWidth(),
        verticalArrangement = Arrangement.spacedBy(12.dp)
    ) {
        when (selectedTab) {
            EmulationMenuTab.Game -> GameTab(
                config = config,
                sessionElapsedMs = sessionElapsedMs,
                callbacks = callbacks
            )
            EmulationMenuTab.Controls -> ControlsTab(config = config, callbacks = callbacks)
            EmulationMenuTab.Display -> DisplayTab(config = config, callbacks = callbacks)
            EmulationMenuTab.System -> SystemTab(config = config, callbacks = callbacks)
            EmulationMenuTab.Achievements -> AchievementsTab(gameId = gameId)
            EmulationMenuTab.Gamepad -> GamepadTab(
                config = config,
                physicalGamepadConnected = physicalGamepadConnected,
                callbacks = callbacks
            )
        }
        Spacer(modifier = Modifier.height(2.dp))
    }
}

private enum class EmulationMenuTab {
    Game,
    Controls,
    Display,
    System,
    Achievements,
    Gamepad
}

private fun Modifier.horizontalBleed(horizontalPadding: Dp): Modifier = layout { measurable, constraints ->
    val bleedPx = horizontalPadding.roundToPx()
    val looseConstraints = constraints.copy(
        maxWidth = (constraints.maxWidth + bleedPx * 2).coerceAtLeast(0)
    )
    val placeable = measurable.measure(looseConstraints)
    layout(constraints.maxWidth, placeable.height) {
        placeable.placeRelative(-bleedPx, 0)
    }
}

@Composable
private fun GameTab(
    config: VitaCoreConfig,
    sessionElapsedMs: Long,
    callbacks: EmulationMenuCallbacks
) {
    MenuSection(
        title = stringResource(R.string.emulation_menu_section_now),
        subtitle = stringResource(R.string.emulation_menu_section_now_desc),
        badge = null
    ) {
        MenuInfoRow(
            label = stringResource(R.string.play_time_current_session),
            value = formatPlayDuration(sessionElapsedMs)
        )
        MenuToggleRow(
            label = stringResource(R.string.settings_core_performance_overlay),
            checked = config.performanceOverlay,
            onCheckedChange = callbacks.onPerformanceOverlay
        )
        if (config.performanceOverlay) {
            MenuChipRow(
                label = stringResource(R.string.settings_core_overlay_detail_title),
                selected = config.performanceOverlayDetail,
                options = listOf(
                    0 to stringResource(R.string.settings_core_overlay_detail_minimum),
                    1 to stringResource(R.string.settings_core_overlay_detail_low),
                    2 to stringResource(R.string.settings_core_overlay_detail_medium),
                    3 to stringResource(R.string.settings_core_overlay_detail_maximum)
                ),
                onSelected = callbacks.onPerformanceDetail
            )
            MenuChipRow(
                label = stringResource(R.string.settings_core_overlay_position_title),
                selected = config.performanceOverlayPosition,
                options = listOf(
                    0 to stringResource(R.string.settings_core_overlay_position_top_left),
                    1 to stringResource(R.string.settings_core_overlay_position_top_center),
                    2 to stringResource(R.string.settings_core_overlay_position_top_right),
                    3 to stringResource(R.string.settings_core_overlay_position_bottom_left),
                    4 to stringResource(R.string.settings_core_overlay_position_bottom_center),
                    5 to stringResource(R.string.settings_core_overlay_position_bottom_right)
                ),
                onSelected = callbacks.onPerformancePosition
            )
        }
        MenuSliderRow(
            label = stringResource(R.string.settings_core_audio_volume_label),
            value = config.audioVolume.toFloat(),
            valueText = "${config.audioVolume}%",
            valueRange = 0f..100f,
            steps = 19,
            onValueChange = { callbacks.onAudioVolume(it.roundToInt()) }
        )
        MenuSliderRow(
            label = stringResource(R.string.settings_bgm_volume),
            value = config.bgmVolume.toFloat(),
            valueText = "${config.bgmVolume}%",
            valueRange = 0f..100f,
            steps = 19,
            onValueChange = { callbacks.onBgmVolume(it.roundToInt()) }
        )
        MenuToggleRow(
            label = stringResource(R.string.settings_show_info_bar),
            checked = config.showInfoBar,
            onCheckedChange = callbacks.onInfoBar
        )
        MenuToggleRow(
            label = stringResource(R.string.settings_core_touchpad_cursor),
            checked = config.showTouchpadCursor,
            onCheckedChange = callbacks.onTouchpadCursor
        )
    }
}

@Composable
private fun MenuInfoRow(
    label: String,
    value: String
) {
    val palette = emulationMenuPalette()
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .clip(RoundedCornerShape(12.dp))
            .background(palette.row)
            .padding(horizontal = 12.dp, vertical = 10.dp),
        verticalAlignment = Alignment.CenterVertically
    ) {
        Text(
            text = label,
            style = MaterialTheme.typography.bodyMedium,
            color = palette.textPrimary,
            modifier = Modifier.weight(1f)
        )
        Text(
            text = value,
            style = MaterialTheme.typography.labelLarge.copy(fontWeight = FontWeight.SemiBold),
            color = MaterialTheme.colorScheme.primary
        )
    }
}

private fun formatPlayDuration(durationMs: Long): String {
    val totalSeconds = (durationMs / 1_000L).coerceAtLeast(0L)
    val hours = totalSeconds / 3_600L
    val minutes = (totalSeconds % 3_600L) / 60L
    val seconds = totalSeconds % 60L
    return when {
        hours > 0L -> "${hours}h ${minutes.toString().padStart(2, '0')}m"
        minutes > 0L -> "${minutes}m ${seconds.toString().padStart(2, '0')}s"
        else -> "${seconds}s"
    }
}

@Composable
private fun ControlsTab(config: VitaCoreConfig, callbacks: EmulationMenuCallbacks) {
    MenuSection(
        title = stringResource(R.string.emulation_menu_section_touch),
        subtitle = stringResource(R.string.emulation_menu_section_touch_desc),
        badge = MenuBadge.Live
    ) {
        MenuActionRow(
            icon = Icons.Rounded.Edit,
            title = stringResource(R.string.emulation_menu_edit_controls),
            subtitle = stringResource(R.string.emulation_menu_edit_controls_desc),
            onClick = callbacks.onEditControls
        )
        MenuActionRow(
            icon = if (config.enableGamepadOverlay) Icons.Rounded.VisibilityOff else Icons.Rounded.Visibility,
            title = stringResource(
                if (config.enableGamepadOverlay) R.string.emulation_menu_hide_controls
                else R.string.emulation_menu_show_controls
            ),
            subtitle = stringResource(R.string.emulation_menu_controls_desc),
            onClick = callbacks.onControlsVisibility
        )
        MenuActionRow(
            icon = Icons.Rounded.Refresh,
            title = stringResource(R.string.emulation_menu_reset_overlay),
            subtitle = stringResource(R.string.emulation_menu_reset_overlay_desc),
            onClick = callbacks.onResetOverlay
        )
        MenuToggleRow(
            label = stringResource(R.string.settings_show_touch_switch),
            checked = config.overlayShowTouchSwitch,
            onCheckedChange = callbacks.onTouchSwitch
        )
        MenuSliderRow(
            label = stringResource(R.string.settings_core_overlay_scale_label),
            value = config.overlayScale,
            valueText = stringResource(R.string.settings_core_overlay_scale_value, config.overlayScale),
            valueRange = 0.5f..2f,
            steps = 14,
            onValueChange = callbacks.onOverlayScale
        )
        MenuSliderRow(
            label = stringResource(R.string.settings_core_overlay_opacity_label),
            value = config.overlayOpacity.toFloat(),
            valueText = stringResource(R.string.settings_core_overlay_opacity_value, config.overlayOpacity),
            valueRange = 10f..100f,
            steps = 8,
            onValueChange = { callbacks.onOverlayOpacity(it.roundToInt()) }
        )
    }
}

@Composable
private fun DisplayTab(config: VitaCoreConfig, callbacks: EmulationMenuCallbacks) {
    MenuSection(
        title = stringResource(R.string.emulation_menu_section_display),
        subtitle = "",
        badge = null
    ) {
        MenuSliderRow(
            label = stringResource(R.string.settings_resolution_multiplier),
            value = config.resolutionMultiplier,
            valueText = stringResource(R.string.emulation_menu_resolution_value, config.resolutionMultiplier),
            valueRange = 1f..8f,
            steps = 13,
            badge = MenuBadge.Restart,
            onValueChange = { callbacks.onResolutionMultiplier((it * 2f).roundToInt() / 2f) }
        )
        MenuToggleRow(
            label = stringResource(R.string.settings_vsync),
            checked = config.vSync,
            onCheckedChange = callbacks.onVsync
        )
        MenuToggleRow(
            label = stringResource(R.string.settings_stretch_display_area),
            checked = config.stretchDisplayArea,
            onCheckedChange = callbacks.onStretchDisplay
        )
        MenuToggleRow(
            label = stringResource(R.string.settings_core_high_accuracy),
            checked = config.highAccuracy,
            badge = MenuBadge.Restart,
            onCheckedChange = callbacks.onHighAccuracy
        )
    }

    MenuSection(
        title = stringResource(R.string.emulation_menu_section_performance),
        subtitle = "",
        badge = MenuBadge.Live
    ) {
        MenuChipRow(
            label = stringResource(R.string.settings_frame_limit),
            selected = config.frameLimit,
            options = listOf(
                0 to stringResource(R.string.settings_frame_limit_unlimited),
                30 to "30 FPS",
                45 to "45 FPS",
                60 to "60 FPS"
            ),
            onSelected = callbacks.onFrameLimit
        )
        MenuToggleRow(
            label = stringResource(R.string.settings_core_fps_hack),
            checked = config.fpsHack,
            onCheckedChange = callbacks.onFpsHack
        )
        MenuToggleRow(
            label = stringResource(R.string.settings_turbo_mode),
            checked = config.turboMode,
            onCheckedChange = callbacks.onTurboMode
        )
        MenuToggleRow(
            label = stringResource(R.string.settings_core_disable_surface_sync),
            checked = config.disableSurfaceSync,
            onCheckedChange = callbacks.onDisableSurfaceSync
        )
        MenuToggleRow(
            label = stringResource(R.string.settings_core_shader_compilation_notice),
            checked = config.showCompileShaders,
            onCheckedChange = callbacks.onShowShaderNotice
        )
    }
}

@Composable
private fun SystemTab(config: VitaCoreConfig, callbacks: EmulationMenuCallbacks) {
    MenuSection(
        title = stringResource(R.string.emulation_menu_section_system),
        subtitle = "",
        badge = null
    ) {
        MenuToggleRow(
            label = stringResource(R.string.settings_pstv_mode),
            checked = config.pstvMode,
            onCheckedChange = callbacks.onPstvMode
        )
        MenuToggleRow(
            label = stringResource(R.string.settings_show_welcome),
            checked = config.showWelcome,
            onCheckedChange = callbacks.onShowWelcome
        )
        MenuToggleRow(
            label = stringResource(R.string.settings_warn_missing_firmware),
            checked = config.warnMissingFirmware,
            onCheckedChange = callbacks.onWarnMissingFirmware
        )
    }
}

@Composable
private fun GamepadTab(
    config: VitaCoreConfig,
    physicalGamepadConnected: Boolean,
    callbacks: EmulationMenuCallbacks
) {
    MenuSection(
        title = stringResource(R.string.emulation_menu_section_gamepad),
        subtitle = if (physicalGamepadConnected) {
            stringResource(R.string.emulation_menu_section_gamepad_desc)
        } else {
            stringResource(R.string.emulation_menu_section_gamepad_disconnected)
        },
        badge = MenuBadge.Live
    ) {
        MenuSliderRow(
            label = stringResource(R.string.settings_gamepad_deadzone),
            value = config.gamepadDeadzone,
            valueText = stringResource(R.string.settings_gamepad_percent_value, (config.gamepadDeadzone * 100f).roundToInt()),
            valueRange = 0f..0.45f,
            steps = 8,
            enabled = physicalGamepadConnected,
            onValueChange = { callbacks.onGamepadDeadzone((it * 100f).roundToInt() / 100f) }
        )
        MenuSliderRow(
            label = stringResource(R.string.settings_core_analog_multiplier_label),
            value = config.analogMultiplier,
            valueText = stringResource(R.string.settings_core_analog_multiplier_value, config.analogMultiplier),
            valueRange = 0.5f..2f,
            steps = 14,
            enabled = physicalGamepadConnected,
            onValueChange = { callbacks.onGamepadAnalogMultiplier((it * 10f).roundToInt() / 10f) }
        )
        MenuSliderRow(
            label = stringResource(R.string.settings_gamepad_trigger_threshold),
            value = config.gamepadTriggerThreshold,
            valueText = stringResource(R.string.settings_gamepad_percent_value, (config.gamepadTriggerThreshold * 100f).roundToInt()),
            valueRange = 0f..0.9f,
            steps = 8,
            enabled = physicalGamepadConnected,
            onValueChange = { callbacks.onGamepadTriggerThreshold((it * 100f).roundToInt() / 100f) }
        )
        MenuChipRow(
            label = stringResource(R.string.settings_gamepad_button_profile),
            selected = when (config.gamepadButtonProfile) {
                VitaCoreConfig.GAMEPAD_PROFILE_SWAP_CROSS_CIRCLE -> 1
                VitaCoreConfig.GAMEPAD_PROFILE_NINTENDO_FACE -> 2
                else -> 0
            },
            options = listOf(
                0 to stringResource(R.string.settings_gamepad_profile_standard),
                1 to stringResource(R.string.settings_gamepad_profile_swap_cross_circle),
                2 to stringResource(R.string.settings_gamepad_profile_nintendo_face)
            ),
            enabled = physicalGamepadConnected,
            onSelected = { value ->
                callbacks.onGamepadButtonProfile(
                    when (value) {
                        1 -> VitaCoreConfig.GAMEPAD_PROFILE_SWAP_CROSS_CIRCLE
                        2 -> VitaCoreConfig.GAMEPAD_PROFILE_NINTENDO_FACE
                        else -> VitaCoreConfig.GAMEPAD_PROFILE_STANDARD
                    }
                )
            }
        )
        MenuToggleRow(
            label = stringResource(R.string.settings_vibration_enable),
            checked = config.gamepadVibration,
            onCheckedChange = callbacks.onGamepadVibration
        )
        MenuSliderRow(
            label = stringResource(R.string.settings_vibration_strength),
            value = config.gamepadVibrationStrength.toFloat(),
            valueText = stringResource(R.string.settings_gamepad_percent_value, config.gamepadVibrationStrength),
            valueRange = 0f..100f,
            steps = 19,
            enabled = config.gamepadVibration,
            onValueChange = { callbacks.onGamepadVibrationStrength(it.roundToInt().coerceIn(0, 100)) }
        )
        MenuToggleRow(
            label = stringResource(R.string.settings_device_vibration_fallback),
            checked = config.deviceVibrationFallback,
            enabled = config.gamepadVibration,
            onCheckedChange = callbacks.onDeviceVibrationFallback
        )
        MenuToggleRow(
            label = stringResource(R.string.settings_gamepad_swap_sticks),
            checked = config.gamepadSwapSticks,
            enabled = physicalGamepadConnected,
            onCheckedChange = callbacks.onGamepadSwapSticks
        )
        MenuToggleRow(
            label = stringResource(R.string.settings_gamepad_invert_left_y),
            checked = config.gamepadInvertLeftY,
            enabled = physicalGamepadConnected,
            onCheckedChange = callbacks.onGamepadInvertLeftY
        )
        MenuToggleRow(
            label = stringResource(R.string.settings_gamepad_invert_right_y),
            checked = config.gamepadInvertRightY,
            enabled = physicalGamepadConnected,
            onCheckedChange = callbacks.onGamepadInvertRightY
        )
    }
}

@Composable
private fun AchievementsTab(gameId: String) {
    val context = LocalContext.current
    var trophySets by remember(gameId) { mutableStateOf<List<VitaTrophySet>>(emptyList()) }
    var isLoading by remember(gameId) { mutableStateOf(true) }

    LaunchedEffect(context, gameId) {
        val repository = TrophyRepository()
        var firstLoad = true
        while (true) {
            if (firstLoad) {
                isLoading = true
            }
            trophySets = if (gameId.isBlank()) {
                emptyList()
            } else {
                withContext(Dispatchers.IO) {
                    repository.loadForTitle(context, gameId)
                }
            }
            firstLoad = false
            isLoading = false
            delay(2_500)
        }
    }

    if (isLoading) {
        MenuSection(
            title = stringResource(R.string.achievements_title),
            subtitle = stringResource(R.string.emulation_achievements_loading),
            badge = null
        ) {
            repeat(4) {
                AchievementMenuSkeleton()
            }
        }
        return
    }

    if (trophySets.isEmpty()) {
        MenuSection(
            title = stringResource(R.string.achievements_title),
            subtitle = stringResource(R.string.emulation_achievements_empty),
            badge = null
        ) {
            MenuEmptyAchievements()
        }
        return
    }

    trophySets.forEach { set ->
        MenuSection(
            title = set.setName.ifBlank { set.gameTitle.ifBlank { stringResource(R.string.achievements_title) } },
            subtitle = stringResource(R.string.achievements_progress_count, set.unlockedCount, set.trophyCount),
            badge = null
        ) {
            AchievementSetProgress(set = set)
            set.groups.forEach { group ->
                if (group.name.isNotBlank() && set.groups.size > 1) {
                    Text(
                        text = group.name,
                        style = MaterialTheme.typography.labelLarge.copy(fontWeight = FontWeight.SemiBold),
                        color = emulationMenuPalette().textSecondary,
                        modifier = Modifier.padding(top = 4.dp)
                    )
                }
                group.trophies.forEach { trophy ->
                    AchievementMenuRow(trophy = trophy)
                }
            }
        }
    }
}

@Composable
private fun AchievementSetProgress(set: VitaTrophySet) {
    val palette = emulationMenuPalette()
    val progress = if (set.trophyCount > 0) set.unlockedCount / set.trophyCount.toFloat() else 0f
    Column(
        modifier = Modifier
            .fillMaxWidth()
            .clip(RoundedCornerShape(12.dp))
            .background(palette.row)
            .padding(horizontal = 12.dp, vertical = 10.dp),
        verticalArrangement = Arrangement.spacedBy(8.dp)
    ) {
        Row(
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(10.dp)
        ) {
            Surface(
                modifier = Modifier.size(42.dp),
                shape = RoundedCornerShape(10.dp),
                color = palette.panelSoft,
                border = BorderStroke(1.dp, palette.border)
            ) {
                LocalImage(
                    path = set.gameIconPath,
                    contentDescription = set.gameTitle,
                    fallbackLabel = set.gameTitle,
                    modifier = Modifier.fillMaxSize()
                )
            }
            Column(modifier = Modifier.weight(1f), verticalArrangement = Arrangement.spacedBy(2.dp)) {
                Text(
                    text = set.gameTitle,
                    style = MaterialTheme.typography.bodyMedium.copy(fontWeight = FontWeight.SemiBold),
                    color = palette.textPrimary,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis
                )
                Text(
                    text = stringResource(R.string.achievements_progress_count, set.unlockedCount, set.trophyCount),
                    style = MaterialTheme.typography.labelMedium,
                    color = MaterialTheme.colorScheme.primary
                )
            }
        }
        LinearProgressIndicator(
            progress = { progress.coerceIn(0f, 1f) },
            modifier = Modifier.fillMaxWidth(),
            color = MaterialTheme.colorScheme.primary,
            trackColor = palette.border
        )
    }
}

@Composable
private fun AchievementMenuRow(trophy: VitaTrophy) {
    val palette = emulationMenuPalette()
    val hiddenLocked = trophy.hidden && !trophy.unlocked
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .clip(RoundedCornerShape(12.dp))
            .background(palette.row)
            .padding(horizontal = 10.dp, vertical = 9.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(10.dp)
    ) {
        Surface(
            modifier = Modifier.size(42.dp),
            shape = RoundedCornerShape(10.dp),
            color = palette.panelSoft,
            border = BorderStroke(1.dp, palette.border)
        ) {
            if (hiddenLocked) {
                Box(modifier = Modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
                    Icon(
                        imageVector = Icons.Rounded.Lock,
                        contentDescription = null,
                        tint = palette.textSecondary,
                        modifier = Modifier.size(20.dp)
                    )
                }
            } else {
                LocalImage(
                    path = trophy.iconPath,
                    contentDescription = trophy.name,
                    fallbackLabel = trophy.name.ifBlank { "?" },
                    modifier = Modifier.fillMaxSize()
                )
            }
        }
        Column(
            modifier = Modifier.weight(1f),
            verticalArrangement = Arrangement.spacedBy(2.dp)
        ) {
            Text(
                text = if (hiddenLocked) stringResource(R.string.achievements_hidden_trophy) else trophy.name.ifBlank { stringResource(R.string.achievements_trophy) },
                style = MaterialTheme.typography.bodyMedium.copy(fontWeight = FontWeight.SemiBold),
                color = if (trophy.unlocked) palette.textPrimary else palette.textSecondary,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis
            )
            Text(
                text = if (hiddenLocked) stringResource(R.string.achievements_hidden_trophy_body) else trophy.detail,
                style = MaterialTheme.typography.bodySmall,
                color = palette.textSecondary,
                maxLines = 2,
                overflow = TextOverflow.Ellipsis
            )
        }
        Row(
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(6.dp)
        ) {
            if (trophy.unlocked) {
                AchievementUnlockedMark()
            }
            AchievementGradeBadge(grade = trophy.grade, unlocked = trophy.unlocked)
        }
    }
}

@Composable
private fun AchievementUnlockedMark() {
    val color = Color(0xFF34D27A)
    Surface(
        modifier = Modifier.size(22.dp),
        shape = CircleShape,
        color = color.copy(alpha = 0.14f),
        border = BorderStroke(1.dp, color.copy(alpha = 0.38f))
    ) {
        Box(
            modifier = Modifier.fillMaxSize(),
            contentAlignment = Alignment.Center
        ) {
            Icon(
                imageVector = Icons.Rounded.CheckCircle,
                contentDescription = null,
                tint = color,
                modifier = Modifier.size(15.dp)
            )
        }
    }
}

@Composable
private fun AchievementGradeBadge(
    grade: VitaTrophyGrade,
    unlocked: Boolean
) {
    val color = if (!unlocked) {
        emulationMenuPalette().textSecondary.copy(alpha = 0.72f)
    } else {
        when (grade) {
            VitaTrophyGrade.Platinum -> Color(0xFF7AD8FF)
            VitaTrophyGrade.Gold -> Color(0xFFE1AA28)
            VitaTrophyGrade.Silver -> Color(0xFFB9C1CB)
            VitaTrophyGrade.Bronze -> Color(0xFFB8794A)
            VitaTrophyGrade.Unknown -> MaterialTheme.colorScheme.primary
        }
    }
    Surface(
        modifier = Modifier.size(34.dp),
        shape = CircleShape,
        color = color.copy(alpha = if (unlocked) 0.18f else 0.08f),
        border = BorderStroke(1.dp, color.copy(alpha = 0.45f))
    ) {
        Box(modifier = Modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
            Text(
                text = when (grade) {
                    VitaTrophyGrade.Platinum -> "P"
                    VitaTrophyGrade.Gold -> "G"
                    VitaTrophyGrade.Silver -> "S"
                    VitaTrophyGrade.Bronze -> "B"
                    VitaTrophyGrade.Unknown -> "-"
                },
                style = MaterialTheme.typography.labelMedium.copy(fontWeight = FontWeight.Bold),
                color = color
            )
        }
    }
}

@Composable
private fun AchievementMenuSkeleton() {
    val palette = emulationMenuPalette()
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .clip(RoundedCornerShape(12.dp))
            .background(palette.row)
            .padding(horizontal = 10.dp, vertical = 9.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(10.dp)
    ) {
        Box(
            modifier = Modifier
                .size(42.dp)
                .clip(RoundedCornerShape(10.dp))
                .background(palette.border)
        )
        Column(modifier = Modifier.weight(1f), verticalArrangement = Arrangement.spacedBy(7.dp)) {
            Box(
                modifier = Modifier
                    .fillMaxWidth(0.7f)
                    .height(12.dp)
                    .clip(RoundedCornerShape(50))
                    .background(palette.border)
            )
            Box(
                modifier = Modifier
                    .fillMaxWidth(0.48f)
                    .height(10.dp)
                    .clip(RoundedCornerShape(50))
                    .background(palette.border.copy(alpha = 0.72f))
            )
        }
    }
}

@Composable
private fun MenuEmptyAchievements() {
    val palette = emulationMenuPalette()
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .clip(RoundedCornerShape(12.dp))
            .background(palette.row)
            .padding(horizontal = 12.dp, vertical = 12.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(10.dp)
    ) {
        Icon(
            imageVector = Icons.Rounded.EmojiEvents,
            contentDescription = null,
            tint = MaterialTheme.colorScheme.primary,
            modifier = Modifier.size(24.dp)
        )
        Text(
            text = stringResource(R.string.emulation_achievements_empty_body),
            style = MaterialTheme.typography.bodySmall,
            color = palette.textSecondary
        )
    }
}

@Composable
private fun MenuTopActions(paused: Boolean, callbacks: EmulationMenuCallbacks) {
    Row(
        modifier = Modifier.fillMaxWidth(),
        horizontalArrangement = Arrangement.spacedBy(10.dp)
    ) {
        MenuTopAction(
            icon = if (paused) Icons.Rounded.PlayArrow else Icons.Rounded.Pause,
            text = stringResource(if (paused) R.string.emulation_resume else R.string.emulation_pause),
            onClick = callbacks.onPauseToggle,
            modifier = Modifier.weight(1f)
        )
        MenuTopAction(
            icon = Icons.AutoMirrored.Rounded.ExitToApp,
            text = stringResource(R.string.emulation_menu_exit_game),
            onClick = callbacks.onExit,
            destructive = true,
            modifier = Modifier.weight(1f)
        )
    }
}

@Composable
private fun MenuTopAction(
    icon: ImageVector,
    text: String,
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
    destructive: Boolean = false
) {
    val palette = emulationMenuPalette()
    val tint = if (destructive) MaterialTheme.colorScheme.error else MaterialTheme.colorScheme.primary
    Surface(
        modifier = modifier.height(44.dp),
        shape = RoundedCornerShape(12.dp),
        color = if (destructive) MaterialTheme.colorScheme.error.copy(alpha = 0.16f) else palette.row,
        border = BorderStroke(1.dp, if (destructive) MaterialTheme.colorScheme.error.copy(alpha = 0.34f) else palette.border),
        onClick = onClick
    ) {
        Row(
            modifier = Modifier.padding(horizontal = 10.dp),
            horizontalArrangement = Arrangement.Center,
            verticalAlignment = Alignment.CenterVertically
        ) {
            Icon(imageVector = icon, contentDescription = null, tint = tint, modifier = Modifier.size(18.dp))
            Text(
                text = text,
                style = MaterialTheme.typography.labelMedium.copy(fontWeight = FontWeight.SemiBold),
                color = if (destructive) MaterialTheme.colorScheme.error else palette.textPrimary,
                modifier = Modifier.padding(start = 7.dp)
            )
        }
    }
}

@Composable
private fun MenuTabs(
    selectedTab: EmulationMenuTab,
    onSelected: (EmulationMenuTab) -> Unit,
    horizontalContentPadding: Dp = 0.dp
) {
    val palette = emulationMenuPalette()
    val tabs = emulationMenuTabs()
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .horizontalScroll(rememberScrollState()),
        horizontalArrangement = Arrangement.spacedBy(8.dp),
        verticalAlignment = Alignment.CenterVertically
    ) {
        if (horizontalContentPadding > 0.dp) {
            Spacer(modifier = Modifier.width(horizontalContentPadding))
        }
        tabs.forEach { item ->
            val selected = selectedTab == item.tab
            Surface(
                onClick = { onSelected(item.tab) },
                shape = RoundedCornerShape(12.dp),
                color = if (selected) {
                    MaterialTheme.colorScheme.primary.copy(alpha = 0.18f)
                } else {
                    Color.Transparent
                },
                border = BorderStroke(
                    1.dp,
                    if (selected) MaterialTheme.colorScheme.primary.copy(alpha = 0.42f) else Color.Transparent
                )
            ) {
                Text(
                    text = item.label,
                    style = MaterialTheme.typography.labelLarge.copy(fontWeight = FontWeight.SemiBold),
                    color = if (selected) MaterialTheme.colorScheme.primary else palette.textSecondary,
                    modifier = Modifier.padding(horizontal = 14.dp, vertical = 9.dp)
                )
            }
        }
        if (horizontalContentPadding > 0.dp) {
            Spacer(modifier = Modifier.width(horizontalContentPadding))
        }
    }
}

@Composable
private fun MenuVerticalTabs(
    selectedTab: EmulationMenuTab,
    onSelected: (EmulationMenuTab) -> Unit,
    iconOnly: Boolean,
    modifier: Modifier = Modifier
) {
    val palette = emulationMenuPalette()
    Column(
        modifier = modifier.verticalScroll(rememberScrollState()),
        verticalArrangement = Arrangement.spacedBy(8.dp)
    ) {
        emulationMenuTabs().forEach { item ->
            val selected = selectedTab == item.tab
            Surface(
                onClick = { onSelected(item.tab) },
                modifier = Modifier.fillMaxWidth(),
                shape = RoundedCornerShape(12.dp),
                color = if (selected) {
                    MaterialTheme.colorScheme.primary.copy(alpha = 0.18f)
                } else {
                    Color.Transparent
                },
                border = BorderStroke(
                    1.dp,
                    if (selected) MaterialTheme.colorScheme.primary.copy(alpha = 0.42f)
                    else palette.border.copy(alpha = 0.42f)
                )
            ) {
                Row(
                    modifier = Modifier.padding(
                        horizontal = if (iconOnly) 10.dp else 12.dp,
                        vertical = 11.dp
                    ),
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = if (iconOnly) Arrangement.Center else Arrangement.spacedBy(10.dp)
                ) {
                    Icon(
                        imageVector = item.icon,
                        contentDescription = if (iconOnly) item.label else null,
                        tint = if (selected) MaterialTheme.colorScheme.primary else palette.textSecondary,
                        modifier = Modifier.size(20.dp)
                    )
                    if (!iconOnly) {
                        Text(
                            text = item.label,
                            style = MaterialTheme.typography.labelLarge.copy(fontWeight = FontWeight.SemiBold),
                            color = if (selected) MaterialTheme.colorScheme.primary else palette.textSecondary,
                            maxLines = 1,
                            overflow = TextOverflow.Ellipsis
                        )
                    }
                }
            }
        }
    }
}

private data class EmulationMenuTabItem(
    val tab: EmulationMenuTab,
    val label: String,
    val icon: ImageVector
)

@Composable
private fun emulationMenuTabs(): List<EmulationMenuTabItem> = listOf(
    EmulationMenuTabItem(EmulationMenuTab.Game, stringResource(R.string.emulation_tab_game), Icons.Rounded.PlayArrow),
    EmulationMenuTabItem(EmulationMenuTab.Controls, stringResource(R.string.emulation_tab_controls), Icons.Rounded.Tune),
    EmulationMenuTabItem(EmulationMenuTab.Display, stringResource(R.string.emulation_tab_display), Icons.Rounded.Visibility),
    EmulationMenuTabItem(EmulationMenuTab.System, stringResource(R.string.emulation_tab_system), Icons.Rounded.Settings),
    EmulationMenuTabItem(
        EmulationMenuTab.Achievements,
        stringResource(R.string.emulation_tab_achievements),
        Icons.Rounded.EmojiEvents
    ),
    EmulationMenuTabItem(
        EmulationMenuTab.Gamepad,
        stringResource(R.string.emulation_tab_gamepad),
        Icons.Rounded.SportsEsports
    )
)

/** Callbacks bag — keeps `EmulationGameMenu` signature compact and testable. */
data class EmulationMenuCallbacks(
    val onPauseToggle: () -> Unit,
    val onExit: () -> Unit,
    val onEditControls: () -> Unit,
    val onControlsVisibility: () -> Unit,
    val onResetOverlay: () -> Unit,
    val onTouchSwitch: (Boolean) -> Unit,
    val onOverlayScale: (Float) -> Unit,
    val onOverlayOpacity: (Int) -> Unit,
    val onPerformanceOverlay: (Boolean) -> Unit,
    val onPerformanceDetail: (Int) -> Unit,
    val onPerformancePosition: (Int) -> Unit,
    val onAudioVolume: (Int) -> Unit,
    val onBgmVolume: (Int) -> Unit,
    val onInfoBar: (Boolean) -> Unit,
    val onTouchpadCursor: (Boolean) -> Unit,
    val onResolutionMultiplier: (Float) -> Unit,
    val onVsync: (Boolean) -> Unit,
    val onStretchDisplay: (Boolean) -> Unit,
    val onHighAccuracy: (Boolean) -> Unit,
    val onFpsHack: (Boolean) -> Unit,
    val onFrameLimit: (Int) -> Unit,
    val onTurboMode: (Boolean) -> Unit,
    val onDisableSurfaceSync: (Boolean) -> Unit,
    val onShowShaderNotice: (Boolean) -> Unit,
    val onPstvMode: (Boolean) -> Unit,
    val onShowWelcome: (Boolean) -> Unit,
    val onWarnMissingFirmware: (Boolean) -> Unit,
    val onGamepadDeadzone: (Float) -> Unit,
    val onGamepadAnalogMultiplier: (Float) -> Unit,
    val onGamepadTriggerThreshold: (Float) -> Unit,
    val onGamepadButtonProfile: (String) -> Unit,
    val onGamepadVibration: (Boolean) -> Unit,
    val onGamepadVibrationStrength: (Int) -> Unit,
    val onDeviceVibrationFallback: (Boolean) -> Unit,
    val onGamepadSwapSticks: (Boolean) -> Unit,
    val onGamepadInvertLeftY: (Boolean) -> Unit,
    val onGamepadInvertRightY: (Boolean) -> Unit
)

private enum class MenuBadge { Live, Restart }

@Composable
private fun SheetHandle() {
    val palette = emulationMenuPalette()
    Box(
        modifier = Modifier
            .fillMaxWidth()
            .height(20.dp),
        contentAlignment = Alignment.Center
    ) {
        Box(
            modifier = Modifier
                .size(width = 42.dp, height = 4.dp)
                .clip(RoundedCornerShape(2.dp))
                .background(palette.textSecondary.copy(alpha = 0.26f))
        )
    }
}

@Composable
private fun MenuHeader(gameTitle: String, gameId: String, paused: Boolean) {
    val palette = emulationMenuPalette()
    Surface(
        shape = RoundedCornerShape(16.dp),
        color = palette.row,
        border = BorderStroke(1.dp, palette.border)
    ) {
        Column(
            modifier = Modifier
                .fillMaxWidth()
                .background(
                    Brush.horizontalGradient(
                        listOf(
                            MaterialTheme.colorScheme.primary.copy(alpha = 0.14f),
                            MaterialTheme.colorScheme.primary.copy(alpha = 0.05f)
                        )
                    )
                )
                .padding(horizontal = 14.dp, vertical = 10.dp),
            verticalArrangement = Arrangement.spacedBy(3.dp)
        ) {
            Row(
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(10.dp)
            ) {
                Text(
                    text = stringResource(R.string.emulation_menu_title),
                    style = MaterialTheme.typography.titleMedium.copy(fontWeight = FontWeight.Bold),
                    color = palette.textPrimary,
                    modifier = Modifier.weight(1f)
                )
                if (paused) {
                    Badge(text = stringResource(R.string.emulation_menu_paused_badge), color = LiveBadgeColor)
                }
            }
            Text(
                text = gameTitle.ifBlank {
                    gameId.ifBlank { stringResource(R.string.emulation_menu_unknown_game) }
                },
                style = MaterialTheme.typography.bodyMedium.copy(fontWeight = FontWeight.Medium),
                color = palette.textSecondary
            )
            if (gameId.isNotBlank() && !gameTitle.equals(gameId, ignoreCase = true)) {
                Text(
                    text = gameId,
                    style = MaterialTheme.typography.labelSmall,
                    color = palette.textSecondary.copy(alpha = 0.72f)
                )
            }
        }
    }
}

@Composable
private fun MenuSection(
    title: String,
    subtitle: String,
    badge: MenuBadge?,
    content: @Composable () -> Unit
) {
    val palette = emulationMenuPalette()
    Surface(
        shape = RoundedCornerShape(16.dp),
        color = palette.panelSoft,
        border = BorderStroke(1.dp, palette.border)
    ) {
        Column(
            modifier = Modifier
                .fillMaxWidth()
                .padding(horizontal = 14.dp, vertical = 12.dp),
            verticalArrangement = Arrangement.spacedBy(9.dp)
        ) {
            Row(
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(8.dp)
            ) {
                Text(
                    text = title,
                    style = MaterialTheme.typography.titleSmall.copy(fontWeight = FontWeight.SemiBold),
                    color = palette.textPrimary,
                    modifier = Modifier.weight(1f)
                )
                when (badge) {
                    MenuBadge.Live -> Badge(
                        text = stringResource(R.string.emulation_menu_badge_live),
                        color = LiveBadgeColor
                    )
                    MenuBadge.Restart -> Badge(
                        text = stringResource(R.string.emulation_menu_badge_restart),
                        color = RestartBadgeColor
                    )
                    null -> Unit
                }
            }
            if (subtitle.isNotBlank()) {
                Text(
                    text = subtitle,
                    style = MaterialTheme.typography.bodySmall,
                    color = palette.textSecondary
                )
                Spacer(modifier = Modifier.height(1.dp))
            }
            content()
        }
    }
}

@Composable
private fun Badge(text: String, color: Color) {
    Surface(
        shape = RoundedCornerShape(50),
        color = color.copy(alpha = 0.18f),
        border = BorderStroke(1.dp, color.copy(alpha = 0.45f))
    ) {
        Text(
            text = text,
            style = MaterialTheme.typography.labelSmall.copy(fontWeight = FontWeight.Medium),
            color = color,
            modifier = Modifier.padding(horizontal = 8.dp, vertical = 2.dp)
        )
    }
}

@Composable
private fun MenuToggleRow(
    label: String,
    checked: Boolean,
    enabled: Boolean = true,
    badge: MenuBadge? = null,
    onCheckedChange: (Boolean) -> Unit
) {
    val palette = emulationMenuPalette()
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .clip(RoundedCornerShape(12.dp))
            .clickable(enabled = enabled) { onCheckedChange(!checked) }
            .background(palette.row)
            .padding(horizontal = 12.dp, vertical = 8.dp),
        verticalAlignment = Alignment.CenterVertically
    ) {
        Text(
            text = label,
            style = MaterialTheme.typography.bodyMedium,
            color = palette.textPrimary.copy(alpha = if (enabled) 1f else 0.48f),
            modifier = Modifier.weight(1f)
        )
        RowBadge(badge)
        Switch(checked = checked, enabled = enabled, onCheckedChange = onCheckedChange)
    }
}

@Composable
private fun MenuSliderRow(
    label: String,
    value: Float,
    valueText: String,
    valueRange: ClosedFloatingPointRange<Float>,
    steps: Int,
    enabled: Boolean = true,
    badge: MenuBadge? = null,
    onValueChange: (Float) -> Unit
) {
    val palette = emulationMenuPalette()
    var pendingValue by remember(label) { mutableFloatStateOf(value) }
    LaunchedEffect(value) {
        pendingValue = value
    }
    Column(
        modifier = Modifier
            .fillMaxWidth()
            .clip(RoundedCornerShape(12.dp))
            .background(palette.row)
            .padding(horizontal = 12.dp, vertical = 9.dp),
        verticalArrangement = Arrangement.spacedBy(2.dp)
    ) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            Text(
                text = label,
                style = MaterialTheme.typography.bodyMedium,
                color = palette.textPrimary.copy(alpha = if (enabled) 1f else 0.48f),
                modifier = Modifier.weight(1f)
            )
            RowBadge(badge)
            Text(
                text = valueText,
                style = MaterialTheme.typography.labelMedium,
                color = palette.textSecondary
            )
        }
        Slider(
            value = pendingValue,
            onValueChange = { pendingValue = it },
            onValueChangeFinished = { onValueChange(pendingValue) },
            enabled = enabled,
            valueRange = valueRange,
            steps = steps
        )
    }
}

@Composable
private fun RowBadge(badge: MenuBadge?) {
    if (badge != null) {
        Box(modifier = Modifier.padding(end = 8.dp)) {
            when (badge) {
                MenuBadge.Live -> Badge(
                    text = stringResource(R.string.emulation_menu_badge_live),
                    color = LiveBadgeColor
                )
                MenuBadge.Restart -> Badge(
                    text = stringResource(R.string.emulation_menu_badge_restart),
                    color = RestartBadgeColor
                )
            }
        }
    }
}

@Composable
private fun MenuChipRow(
    label: String,
    selected: Int,
    options: List<Pair<Int, String>>,
    enabled: Boolean = true,
    onSelected: (Int) -> Unit
) {
    val palette = emulationMenuPalette()
    Column(verticalArrangement = Arrangement.spacedBy(6.dp)) {
        Text(
            text = label,
            style = MaterialTheme.typography.labelLarge,
            color = palette.textSecondary
        )
        FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            options.forEach { (value, text) ->
                FilterChip(
                    selected = selected == value,
                    onClick = { onSelected(value) },
                    enabled = enabled,
                    label = { Text(text) }
                )
            }
        }
    }
}

@Composable
private fun MenuActionRow(
    icon: ImageVector,
    title: String,
    subtitle: String,
    onClick: () -> Unit,
    destructive: Boolean = false
) {
    val palette = emulationMenuPalette()
    val containerColor = if (destructive) {
        MaterialTheme.colorScheme.error.copy(alpha = 0.14f)
    } else {
        palette.row
    }
    val tint = if (destructive) MaterialTheme.colorScheme.error else palette.textPrimary
    Surface(
        modifier = Modifier
            .fillMaxWidth()
            .clip(RoundedCornerShape(14.dp))
            .clickable(onClick = onClick),
        color = containerColor,
        border = BorderStroke(1.dp, palette.border)
    ) {
        Row(
            modifier = Modifier.padding(horizontal = 12.dp, vertical = 10.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            Icon(imageVector = icon, contentDescription = null, tint = tint, modifier = Modifier.size(22.dp))
            Column(modifier = Modifier.weight(1f), verticalArrangement = Arrangement.spacedBy(2.dp)) {
                Text(
                    text = title,
                    style = MaterialTheme.typography.bodyLarge.copy(fontWeight = FontWeight.Medium),
                    color = tint
                )
                Text(
                    text = subtitle,
                    style = MaterialTheme.typography.bodySmall,
                    color = palette.textSecondary
                )
            }
        }
    }
}

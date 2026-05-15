package com.sbro.emucorev.ui.emulation

import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.asPaddingValues
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.navigationBars
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.statusBars
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.rounded.ExitToApp
import androidx.compose.material.icons.rounded.CameraAlt
import androidx.compose.material.icons.rounded.Edit
import androidx.compose.material.icons.rounded.Pause
import androidx.compose.material.icons.rounded.PlayArrow
import androidx.compose.material.icons.rounded.Refresh
import androidx.compose.material.icons.rounded.Tune
import androidx.compose.material.icons.rounded.Visibility
import androidx.compose.material.icons.rounded.VisibilityOff
import androidx.compose.material3.FilterChip
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Slider
import androidx.compose.material3.Surface
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import com.sbro.emucorev.R
import com.sbro.emucorev.core.VitaCoreConfig
import kotlin.math.roundToInt

// Modern pause menu for live emulation.
//
// Surface roles:
//   - `EmulationQuickBar` is the always-visible toolbar shown while the game is
//     suspended; it exposes the few actions a player reaches for the most
//     (pause/resume, screenshot, open full menu).
//   - `EmulationGameMenu` is the full menu, rendered as a slide-up sheet from
//     the bottom on phones and as a slide-in panel from the right on tablets.
//
// Two badge styles communicate behavior to the user:
//   - LIVE   — change is applied to the running game immediately.
//   - RESTART — change is persisted to config.yml; effect applies on next launch.

internal val EmulationMenuPanel = Color(0xFF17171D).copy(alpha = 0.96f)
internal val EmulationMenuPanelSoft = Color(0xFF22222B).copy(alpha = 0.92f)
internal val EmulationMenuBorder = Color.White.copy(alpha = 0.08f)
internal val EmulationMenuTextPrimary = Color(0xFFF4F4F7)
internal val EmulationMenuTextSecondary = Color(0xFFB7B7C9)
private val LiveBadgeColor = Color(0xFF34D27A)
private val RestartBadgeColor = Color(0xFFE0A82E)

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
    val topInset = WindowInsets.statusBars.asPaddingValues().calculateTopPadding()
    Surface(
        modifier = modifier.padding(top = topInset + 12.dp),
        shape = RoundedCornerShape(28.dp),
        color = EmulationMenuPanel,
        border = BorderStroke(1.dp, EmulationMenuBorder),
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
                onClick = onPauseToggle
            )
            QuickBarButton(
                icon = Icons.Rounded.CameraAlt,
                contentDescription = stringResource(R.string.emulation_quickbar_screenshot),
                onClick = onScreenshot
            )
            QuickBarButton(
                icon = Icons.Rounded.Tune,
                contentDescription = stringResource(R.string.emulation_quickbar_open_menu),
                onClick = onOpenMenu
            )
        }
    }
}

@Composable
private fun QuickBarButton(
    icon: ImageVector,
    contentDescription: String,
    onClick: () -> Unit
) {
    Box(
        modifier = Modifier
            .size(48.dp)
            .clip(CircleShape)
            .background(Color.White.copy(alpha = 0.05f))
            .clickable(onClick = onClick),
        contentAlignment = Alignment.Center
    ) {
        Icon(
            imageVector = icon,
            contentDescription = contentDescription,
            tint = EmulationMenuTextPrimary,
            modifier = Modifier.size(22.dp)
        )
    }
}

/**
 * Full game menu, rendered as a translucent sheet anchored to the bottom (or
 * right edge on tablets). The composable returns a card; the host is
 * responsible for animating it in/out and for darkening the scrim behind.
 */
@Composable
fun EmulationGameMenu(
    gameId: String,
    config: VitaCoreConfig,
    paused: Boolean,
    controlsEditMode: Boolean,
    expandHorizontally: Boolean,
    callbacks: EmulationMenuCallbacks,
    modifier: Modifier = Modifier
) {
    val navInsets = WindowInsets.navigationBars.asPaddingValues()
    val shape = if (expandHorizontally) {
        RoundedCornerShape(topStart = 28.dp, bottomStart = 28.dp)
    } else {
        RoundedCornerShape(topStart = 28.dp, topEnd = 28.dp)
    }
    Surface(
        modifier = modifier
            .then(
                if (expandHorizontally) {
                    Modifier
                        .fillMaxHeight()
                        .width(440.dp)
                } else {
                    Modifier
                        .fillMaxWidth()
                        .heightIn(max = 620.dp)
                }
            ),
        shape = shape,
        color = EmulationMenuPanel,
        border = BorderStroke(1.dp, EmulationMenuBorder),
        tonalElevation = 6.dp,
        shadowElevation = 18.dp
    ) {
        Column(
            modifier = Modifier
                .fillMaxSize()
                .verticalScroll(rememberScrollState())
                .padding(
                    start = 18.dp,
                    end = 18.dp,
                    top = 14.dp,
                    bottom = 18.dp + navInsets.calculateBottomPadding()
                ),
            verticalArrangement = Arrangement.spacedBy(14.dp)
        ) {
            if (!expandHorizontally) {
                SheetHandle()
            }
            MenuHeader(gameId = gameId, paused = paused)

            MenuSection(
                title = stringResource(R.string.emulation_menu_section_now),
                subtitle = stringResource(R.string.emulation_menu_section_now_desc),
                badge = MenuBadge.Live
            ) {
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

            MenuSection(
                title = stringResource(R.string.emulation_menu_section_display),
                subtitle = stringResource(R.string.emulation_menu_section_display_desc),
                badge = MenuBadge.Restart
            ) {
                MenuSliderRow(
                    label = stringResource(R.string.settings_resolution_multiplier),
                    value = config.resolutionMultiplier,
                    valueText = stringResource(R.string.emulation_menu_resolution_value, config.resolutionMultiplier),
                    valueRange = 1f..8f,
                    steps = 13,
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
                    onCheckedChange = callbacks.onHighAccuracy
                )
            }

            MenuSection(
                title = stringResource(R.string.emulation_menu_section_performance),
                subtitle = stringResource(R.string.emulation_menu_section_performance_desc),
                badge = MenuBadge.Restart
            ) {
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

            MenuSection(
                title = stringResource(R.string.emulation_menu_section_system),
                subtitle = stringResource(R.string.emulation_menu_section_system_desc),
                badge = MenuBadge.Restart
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

            MenuSection(
                title = stringResource(R.string.emulation_menu_section_session),
                subtitle = stringResource(R.string.emulation_menu_section_session_desc),
                badge = null
            ) {
                MenuActionRow(
                    icon = if (paused) Icons.Rounded.PlayArrow else Icons.Rounded.Pause,
                    title = stringResource(
                        if (paused) R.string.emulation_resume else R.string.emulation_pause
                    ),
                    subtitle = stringResource(R.string.emulation_menu_pause_desc),
                    onClick = callbacks.onPauseToggle
                )
                MenuActionRow(
                    icon = Icons.AutoMirrored.Rounded.ExitToApp,
                    title = stringResource(R.string.emulation_menu_exit_game),
                    subtitle = stringResource(R.string.emulation_menu_exit_game_desc),
                    onClick = callbacks.onExit,
                    destructive = true
                )
            }
        }
    }
}

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
    val onTurboMode: (Boolean) -> Unit,
    val onDisableSurfaceSync: (Boolean) -> Unit,
    val onShowShaderNotice: (Boolean) -> Unit,
    val onPstvMode: (Boolean) -> Unit,
    val onShowWelcome: (Boolean) -> Unit,
    val onWarnMissingFirmware: (Boolean) -> Unit
)

private enum class MenuBadge { Live, Restart }

@Composable
private fun SheetHandle() {
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
                .background(Color.White.copy(alpha = 0.16f))
        )
    }
}

@Composable
private fun MenuHeader(gameId: String, paused: Boolean) {
    Surface(
        shape = RoundedCornerShape(20.dp),
        color = Color.White.copy(alpha = 0.04f),
        border = BorderStroke(1.dp, Color.White.copy(alpha = 0.05f))
    ) {
        Column(
            modifier = Modifier
                .fillMaxWidth()
                .background(
                    Brush.horizontalGradient(
                        listOf(
                            MaterialTheme.colorScheme.primary.copy(alpha = 0.18f),
                            MaterialTheme.colorScheme.primary.copy(alpha = 0.08f)
                        )
                    )
                )
                .padding(horizontal = 18.dp, vertical = 14.dp),
            verticalArrangement = Arrangement.spacedBy(4.dp)
        ) {
            Row(
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(10.dp)
            ) {
                Text(
                    text = stringResource(R.string.emulation_menu_title),
                    style = MaterialTheme.typography.titleLarge.copy(fontWeight = FontWeight.SemiBold),
                    color = EmulationMenuTextPrimary,
                    modifier = Modifier.weight(1f)
                )
                if (paused) {
                    Badge(text = stringResource(R.string.emulation_menu_paused_badge), color = LiveBadgeColor)
                }
            }
            Text(
                text = gameId.ifBlank { stringResource(R.string.emulation_menu_unknown_game) },
                style = MaterialTheme.typography.bodyMedium,
                color = EmulationMenuTextSecondary
            )
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
    Surface(
        shape = RoundedCornerShape(22.dp),
        color = EmulationMenuPanelSoft,
        border = BorderStroke(1.dp, EmulationMenuBorder)
    ) {
        Column(
            modifier = Modifier
                .fillMaxWidth()
                .padding(horizontal = 16.dp, vertical = 14.dp),
            verticalArrangement = Arrangement.spacedBy(10.dp)
        ) {
            Row(
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(8.dp)
            ) {
                Text(
                    text = title,
                    style = MaterialTheme.typography.titleSmall.copy(fontWeight = FontWeight.SemiBold),
                    color = EmulationMenuTextPrimary,
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
            Text(
                text = subtitle,
                style = MaterialTheme.typography.bodySmall,
                color = EmulationMenuTextSecondary
            )
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
    onCheckedChange: (Boolean) -> Unit
) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .clip(RoundedCornerShape(14.dp))
            .clickable { onCheckedChange(!checked) }
            .padding(vertical = 4.dp),
        verticalAlignment = Alignment.CenterVertically
    ) {
        Text(
            text = label,
            style = MaterialTheme.typography.bodyMedium,
            color = EmulationMenuTextPrimary,
            modifier = Modifier.weight(1f)
        )
        Switch(checked = checked, onCheckedChange = onCheckedChange)
    }
}

@Composable
private fun MenuSliderRow(
    label: String,
    value: Float,
    valueText: String,
    valueRange: ClosedFloatingPointRange<Float>,
    steps: Int,
    onValueChange: (Float) -> Unit
) {
    Column(verticalArrangement = Arrangement.spacedBy(2.dp)) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            Text(
                text = label,
                style = MaterialTheme.typography.bodyMedium,
                color = EmulationMenuTextPrimary,
                modifier = Modifier.weight(1f)
            )
            Text(
                text = valueText,
                style = MaterialTheme.typography.labelMedium,
                color = EmulationMenuTextSecondary
            )
        }
        Slider(
            value = value,
            onValueChange = onValueChange,
            valueRange = valueRange,
            steps = steps
        )
    }
}

@Composable
private fun MenuChipRow(
    label: String,
    selected: Int,
    options: List<Pair<Int, String>>,
    onSelected: (Int) -> Unit
) {
    Column(verticalArrangement = Arrangement.spacedBy(6.dp)) {
        Text(
            text = label,
            style = MaterialTheme.typography.labelLarge,
            color = EmulationMenuTextSecondary
        )
        FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            options.forEach { (value, text) ->
                FilterChip(
                    selected = selected == value,
                    onClick = { onSelected(value) },
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
    val containerColor = if (destructive) {
        MaterialTheme.colorScheme.error.copy(alpha = 0.14f)
    } else {
        Color.White.copy(alpha = 0.05f)
    }
    val tint = if (destructive) MaterialTheme.colorScheme.error else EmulationMenuTextPrimary
    Surface(
        modifier = Modifier
            .fillMaxWidth()
            .clip(RoundedCornerShape(16.dp))
            .clickable(onClick = onClick),
        color = containerColor,
        border = BorderStroke(1.dp, Color.White.copy(alpha = 0.04f))
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
                    color = EmulationMenuTextSecondary
                )
            }
        }
    }
}

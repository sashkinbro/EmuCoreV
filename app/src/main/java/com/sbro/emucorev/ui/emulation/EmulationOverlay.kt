package com.sbro.emucorev.ui.emulation

import android.annotation.SuppressLint
import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.animation.core.tween
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.slideInHorizontally
import androidx.compose.animation.slideOutHorizontally
import androidx.compose.foundation.Image
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.gestures.detectDragGestures
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.asPaddingValues
import androidx.compose.foundation.layout.displayCutout
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.navigationBars
import androidx.compose.foundation.layout.offset
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
import androidx.compose.material.icons.rounded.Gamepad
import androidx.compose.material.icons.rounded.Menu
import androidx.compose.material.icons.rounded.Pause
import androidx.compose.material.icons.rounded.PlayArrow
import androidx.compose.material.icons.rounded.Refresh
import androidx.compose.material.icons.rounded.SettingsSuggest
import androidx.compose.material.icons.rounded.Visibility
import androidx.compose.material.icons.rounded.VisibilityOff
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.FilterChip
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Slider
import androidx.compose.material3.Surface
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Rect
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Shape
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.input.pointer.pointerInteropFilter
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.layout.onSizeChanged
import androidx.compose.ui.platform.LocalConfiguration
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.IntOffset
import androidx.compose.ui.unit.dp
import android.view.MotionEvent
import com.sbro.emucorev.R
import com.sbro.emucorev.core.VitaCoreConfig
import com.sbro.emucorev.core.VitaCoreConfigRepository
import com.sbro.emucorev.core.vita.Emulator
import com.sbro.emucorev.core.vita.overlay.InputOverlay
import kotlin.math.abs
import kotlin.math.roundToInt

private enum class EmulationMenuTab {
    Session,
    Overlay,
    Graphics,
    System
}

private val EmulationPanel = Color(0xFF17171D).copy(alpha = 0.96f)
private val EmulationPanelSoft = Color(0xFF22222B).copy(alpha = 0.92f)
private val EmulationPanelBorder = Color.White.copy(alpha = 0.08f)
private val EmulationTextPrimary = Color(0xFFF4F4F7)
private val EmulationTextSecondary = Color(0xFFB7B7C9)

@Composable
fun EmulationOverlayHost(
    activity: Emulator,
    modifier: Modifier = Modifier
) {
    val repository = remember(activity) { VitaCoreConfigRepository(activity) }
    val overlayBridge = remember(activity) { activity.getmOverlay() }
    var config by remember(activity) { mutableStateOf(repository.load()) }
    var menuOpen by remember { mutableStateOf(false) }
    var menuButtonVisible by remember { mutableStateOf(true) }
    var pausedByMenu by remember { mutableStateOf(false) }
    var activeTab by remember { mutableStateOf(EmulationMenuTab.Session) }
    var backTouchEnabled by remember { mutableStateOf(false) }
    var exitDialogVisible by remember { mutableStateOf(false) }
    val gameId = remember(activity) { activity.currentGameIdOrIntent() }
    val hasPhysicalGamepad = activity.hasPhysicalGamepad
    val showTouchControls = config.enableGamepadOverlay &&
        overlayBridge.effectiveOverlayMask != 0 &&
        !menuOpen &&
        !hasPhysicalGamepad

    fun persistConfig(transform: (VitaCoreConfig) -> VitaCoreConfig) {
        config = transform(config)
        repository.save(config)
    }

    fun syncPerformanceOverlayState() {
        activity.setPerformanceOverlayState(
            config.performanceOverlay,
            config.performanceOverlayDetail,
            config.performanceOverlayPosition
        )
    }

    DisposableEffect(config) {
        overlayBridge.synchronizeConfig(config)
        syncPerformanceOverlayState()
        onDispose {}
    }

    LaunchedEffect(menuOpen) {
        if (menuOpen && !pausedByMenu) {
            activity.setMenuPaused(true)
            pausedByMenu = true
        } else if (!menuOpen && pausedByMenu) {
            activity.setMenuPaused(false)
            pausedByMenu = false
        }
        if (menuOpen) {
            menuButtonVisible = true
        }
    }

    LaunchedEffect(menuOpen, menuButtonVisible) {
        if (!menuOpen && menuButtonVisible) {
            kotlinx.coroutines.delay(5_000)
            if (!menuOpen) {
                menuButtonVisible = false
            }
        }
    }

    DisposableEffect(activity) {
        activity.setOverlayBackHandler {
            if (exitDialogVisible) {
                exitDialogVisible = false
                true
            } else {
            activeTab = EmulationMenuTab.Session
            menuOpen = !menuOpen
            menuButtonVisible = true
            true
            }
        }
        activity.setOverlayMenuButtonRevealHandler {
            menuButtonVisible = true
        }
        onDispose {
            activity.setOverlayBackHandler(null)
            activity.setOverlayMenuButtonRevealHandler(null)
        }
    }

    Box(modifier = modifier.fillMaxSize()) {
        if (showTouchControls) {
            OnScreenControls(
                modifier = Modifier.fillMaxSize(),
                overlayScale = config.overlayScale,
                overlayOpacity = config.overlayOpacity,
                showTouchSwitch = config.overlayShowTouchSwitch,
                backTouchEnabled = backTouchEnabled,
                onBackTouchToggle = {
                    backTouchEnabled = !backTouchEnabled
                    overlayBridge.setTouchState(backTouchEnabled)
                },
                onButtonChange = { button, pressed -> overlayBridge.setButton(button, pressed) },
                onAxisChange = { axis, value -> overlayBridge.setAxis(axis, value) }
            )
        }

        AnimatedVisibility(
            visible = menuButtonVisible || menuOpen,
            enter = fadeIn(tween(180)),
            exit = fadeOut(tween(140)),
            modifier = Modifier.align(Alignment.TopCenter)
        ) {
            TopHud(
                menuOpen = menuOpen,
                onMenuClick = {
                    activeTab = EmulationMenuTab.Session
                    menuOpen = !menuOpen
                    menuButtonVisible = true
                }
            )
        }

        AnimatedVisibility(visible = menuOpen, enter = fadeIn(tween(220)), exit = fadeOut(tween(180))) {
            Box(
                modifier = Modifier
                    .fillMaxSize()
                    .background(Color.Black.copy(alpha = 0.5f))
                    .clickable(interactionSource = remember { MutableInteractionSource() }, indication = null) {
                        menuOpen = false
                    }
            )
        }

        AnimatedVisibility(
            visible = menuOpen,
            enter = slideInHorizontally(initialOffsetX = { it }) + fadeIn(tween(220)),
            exit = slideOutHorizontally(targetOffsetX = { it }) + fadeOut(tween(180)),
            modifier = Modifier.align(Alignment.CenterEnd)
        ) {
            EmulationSidebarMenu(
                gameId = gameId,
                config = config,
                activeTab = activeTab,
                controlsVisible = config.enableGamepadOverlay,
                paused = pausedByMenu,
                onTabSelected = { activeTab = it },
                onPauseToggle = {
                    val next = !pausedByMenu
                    activity.setMenuPaused(next)
                    pausedByMenu = next
                },
                onControlsVisibilityToggle = {
                    persistConfig { it.copy(enableGamepadOverlay = !it.enableGamepadOverlay) }
                },
                onResetOverlay = {
                    persistConfig {
                        it.copy(
                            enableGamepadOverlay = true,
                            overlayShowTouchSwitch = false,
                            overlayScale = 0.9f,
                            overlayOpacity = 100
                        )
                    }
                    backTouchEnabled = false
                    overlayBridge.setTouchState(false)
                },
                onClose = { menuOpen = false },
                onExit = { exitDialogVisible = true },
                onOverlayScaleChange = { value -> persistConfig { it.copy(overlayScale = value) } },
                onOverlayOpacityChange = { value -> persistConfig { it.copy(overlayOpacity = value) } },
                onTouchSwitchChange = { enabled ->
                    persistConfig { it.copy(overlayShowTouchSwitch = enabled) }
                    if (!enabled) {
                        backTouchEnabled = false
                        overlayBridge.setTouchState(false)
                    }
                },
                onPerformanceOverlayChange = { enabled ->
                    persistConfig { it.copy(performanceOverlay = enabled) }
                    syncPerformanceOverlayState()
                },
                onPerformanceDetailChange = { value ->
                    persistConfig { it.copy(performanceOverlayDetail = value) }
                    syncPerformanceOverlayState()
                },
                onPerformancePositionChange = { value ->
                    persistConfig { it.copy(performanceOverlayPosition = value) }
                    syncPerformanceOverlayState()
                },
                onResolutionMultiplierChange = { value -> persistConfig { it.copy(resolutionMultiplier = value) } },
                onHighAccuracyChange = { enabled -> persistConfig { it.copy(highAccuracy = enabled) } },
                onDisableSurfaceSyncChange = { enabled -> persistConfig { it.copy(disableSurfaceSync = enabled) } },
                onShowShaderNoticeChange = { enabled -> persistConfig { it.copy(showCompileShaders = enabled) } },
                onFpsHackChange = { enabled -> persistConfig { it.copy(fpsHack = enabled) } },
                onStretchDisplayChange = { enabled -> persistConfig { it.copy(stretchDisplayArea = enabled) } },
                onVsyncChange = { enabled -> persistConfig { it.copy(vSync = enabled) } },
                onPstvModeChange = { enabled -> persistConfig { it.copy(pstvMode = enabled) } },
                onInfoBarChange = { enabled -> persistConfig { it.copy(showInfoBar = enabled) } },
                onTurboModeChange = { enabled -> persistConfig { it.copy(turboMode = enabled) } },
                onTouchpadCursorChange = { enabled -> persistConfig { it.copy(showTouchpadCursor = enabled) } }
            )
        }

        if (exitDialogVisible) {
            AlertDialog(
                onDismissRequest = { exitDialogVisible = false },
                title = {
                    Text(text = stringResource(R.string.emulation_exit_confirm_title))
                },
                text = {
                    Text(text = stringResource(R.string.emulation_exit_confirm_body))
                },
                confirmButton = {
                    TextButton(
                        onClick = {
                            exitDialogVisible = false
                            activity.exitEmulation()
                        }
                    ) {
                        Text(text = stringResource(R.string.emulation_exit_confirm_action))
                    }
                },
                dismissButton = {
                    TextButton(onClick = { exitDialogVisible = false }) {
                        Text(text = stringResource(R.string.emulation_exit_cancel_action))
                    }
                }
            )
        }
    }

    LaunchedEffect(hasPhysicalGamepad) {
        if (hasPhysicalGamepad && backTouchEnabled) {
            backTouchEnabled = false
            overlayBridge.setTouchState(false)
        }
    }
}

@Composable
private fun TopHud(
    menuOpen: Boolean,
    onMenuClick: () -> Unit,
    modifier: Modifier = Modifier
) {
    val statusInsets = WindowInsets.statusBars.asPaddingValues()
    Row(
        modifier = modifier
            .fillMaxWidth()
            .padding(top = statusInsets.calculateTopPadding() + 18.dp, start = 18.dp, end = 18.dp),
        horizontalArrangement = Arrangement.SpaceBetween,
        verticalAlignment = Alignment.CenterVertically
    ) {
        Spacer(modifier = Modifier.size(20.dp))

        Surface(
            modifier = Modifier.clickable(onClick = onMenuClick),
            color = Color.Transparent,
            shape = RoundedCornerShape(14.dp),
            border = BorderStroke(1.dp, Color.White.copy(alpha = if (menuOpen) 0.68f else 0.42f))
        ) {
            Box(modifier = Modifier.size(width = 44.dp, height = 38.dp), contentAlignment = Alignment.Center) {
                Icon(imageVector = Icons.Rounded.Menu, contentDescription = null, tint = Color.White.copy(alpha = 0.96f))
            }
        }
    }
}

@Composable
private fun EmulationSidebarMenu(
    gameId: String,
    config: VitaCoreConfig,
    activeTab: EmulationMenuTab,
    controlsVisible: Boolean,
    paused: Boolean,
    onTabSelected: (EmulationMenuTab) -> Unit,
    onPauseToggle: () -> Unit,
    onControlsVisibilityToggle: () -> Unit,
    onResetOverlay: () -> Unit,
    onClose: () -> Unit,
    onExit: () -> Unit,
    onOverlayScaleChange: (Float) -> Unit,
    onOverlayOpacityChange: (Int) -> Unit,
    onTouchSwitchChange: (Boolean) -> Unit,
    onPerformanceOverlayChange: (Boolean) -> Unit,
    onPerformanceDetailChange: (Int) -> Unit,
    onPerformancePositionChange: (Int) -> Unit,
    onResolutionMultiplierChange: (Float) -> Unit,
    onHighAccuracyChange: (Boolean) -> Unit,
    onDisableSurfaceSyncChange: (Boolean) -> Unit,
    onShowShaderNoticeChange: (Boolean) -> Unit,
    onFpsHackChange: (Boolean) -> Unit,
    onStretchDisplayChange: (Boolean) -> Unit,
    onVsyncChange: (Boolean) -> Unit,
    onPstvModeChange: (Boolean) -> Unit,
    onInfoBarChange: (Boolean) -> Unit,
    onTurboModeChange: (Boolean) -> Unit,
    onTouchpadCursorChange: (Boolean) -> Unit
) {
    val navInsets = WindowInsets.navigationBars.asPaddingValues()
    Row(
        modifier = Modifier
            .fillMaxHeight()
            .padding(top = 16.dp, bottom = 16.dp + navInsets.calculateBottomPadding(), end = 16.dp),
        horizontalArrangement = Arrangement.End,
        verticalAlignment = Alignment.Top
    ) {
        Surface(
            modifier = Modifier.fillMaxHeight().width(410.dp),
            shape = RoundedCornerShape(28.dp),
            color = EmulationPanel,
            border = BorderStroke(1.dp, EmulationPanelBorder)
        ) {
            Column(
                modifier = Modifier
                    .fillMaxSize()
                    .verticalScroll(rememberScrollState())
                    .padding(horizontal = 18.dp, vertical = 18.dp),
                verticalArrangement = Arrangement.spacedBy(16.dp)
            ) {
                Surface(
                    shape = RoundedCornerShape(22.dp),
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
                            .padding(horizontal = 18.dp, vertical = 18.dp),
                        verticalArrangement = Arrangement.spacedBy(4.dp)
                    ) {
                        Text(
                            text = stringResource(R.string.emulation_menu_title),
                            style = MaterialTheme.typography.headlineSmall.copy(fontWeight = FontWeight.Bold),
                            color = EmulationTextPrimary
                        )
                        Text(
                            text = gameId.ifBlank { stringResource(R.string.emulation_menu_unknown_game) },
                            style = MaterialTheme.typography.bodyMedium,
                            color = EmulationTextSecondary
                        )
                    }
                }

                MenuSectionCard(
                    title = when (activeTab) {
                        EmulationMenuTab.Session -> stringResource(R.string.emulation_tab_session)
                        EmulationMenuTab.Overlay -> stringResource(R.string.emulation_tab_overlay)
                        EmulationMenuTab.Graphics -> stringResource(R.string.emulation_tab_graphics)
                        EmulationMenuTab.System -> stringResource(R.string.emulation_tab_system)
                    },
                    subtitle = when (activeTab) {
                        EmulationMenuTab.Session -> stringResource(R.string.emulation_menu_pause_desc)
                        EmulationMenuTab.Overlay -> stringResource(R.string.emulation_menu_overlay_live)
                        EmulationMenuTab.Graphics -> stringResource(R.string.emulation_menu_next_launch)
                        EmulationMenuTab.System -> stringResource(R.string.emulation_menu_next_launch)
                    }
                ) {}

                when (activeTab) {
                    EmulationMenuTab.Session -> {
                        MenuActionButton(
                            icon = if (paused) Icons.Rounded.PlayArrow else Icons.Rounded.Pause,
                            title = stringResource(if (paused) R.string.emulation_resume else R.string.emulation_pause),
                            subtitle = stringResource(R.string.emulation_menu_pause_desc),
                            onClick = onPauseToggle
                        )
                        MenuActionButton(
                            icon = Icons.AutoMirrored.Rounded.ExitToApp,
                            title = stringResource(R.string.emulation_menu_exit_game),
                            subtitle = stringResource(R.string.emulation_menu_exit_game_desc),
                            onClick = onExit,
                            containerColor = MaterialTheme.colorScheme.error.copy(alpha = 0.14f)
                        )
                    }

                    EmulationMenuTab.Overlay -> {
                        MenuActionButton(
                            icon = if (controlsVisible) Icons.Rounded.VisibilityOff else Icons.Rounded.Visibility,
                            title = stringResource(if (controlsVisible) R.string.emulation_menu_hide_controls else R.string.emulation_menu_show_controls),
                            subtitle = stringResource(R.string.emulation_menu_controls_desc),
                            onClick = onControlsVisibilityToggle
                        )
                        MenuActionButton(
                            icon = Icons.Rounded.Refresh,
                            title = stringResource(R.string.emulation_menu_reset_overlay),
                            subtitle = stringResource(R.string.emulation_menu_reset_overlay_desc),
                            onClick = onResetOverlay
                        )
                        MenuSectionCard(
                            title = stringResource(R.string.settings_tab_overlay),
                            subtitle = stringResource(R.string.emulation_menu_overlay_live)
                        ) {
                            ToggleRow(stringResource(R.string.settings_core_gamepad_overlay), controlsVisible) { onControlsVisibilityToggle() }
                            ToggleRow(stringResource(R.string.settings_show_touch_switch), config.overlayShowTouchSwitch, onTouchSwitchChange)
                            SliderRow(
                                title = stringResource(R.string.settings_core_overlay_scale_label),
                                value = config.overlayScale,
                                valueText = stringResource(R.string.settings_core_overlay_scale_value, config.overlayScale),
                                valueRange = 0.5f..2f,
                                steps = 14,
                                onValueChange = onOverlayScaleChange
                            )
                            SliderRow(
                                title = stringResource(R.string.settings_core_overlay_opacity_label),
                                value = config.overlayOpacity.toFloat(),
                                valueText = stringResource(R.string.settings_core_overlay_opacity_value, config.overlayOpacity),
                                valueRange = 10f..100f,
                                steps = 8,
                                onValueChange = { onOverlayOpacityChange(it.roundToInt()) }
                            )
                        }
                    }

                    EmulationMenuTab.Graphics -> {
                        MenuSectionCard(
                            title = stringResource(R.string.emulation_tab_graphics),
                            subtitle = stringResource(R.string.emulation_menu_next_launch)
                        ) {
                            SliderRow(
                                title = stringResource(R.string.settings_resolution_multiplier),
                                value = config.resolutionMultiplier,
                                valueText = stringResource(R.string.emulation_menu_resolution_value, config.resolutionMultiplier),
                                valueRange = 1f..8f,
                                steps = 13,
                                onValueChange = { value -> onResolutionMultiplierChange((value * 2f).roundToInt() / 2f) }
                            )
                            ToggleRow(stringResource(R.string.settings_core_high_accuracy), config.highAccuracy, onHighAccuracyChange)
                            ToggleRow(stringResource(R.string.settings_core_disable_surface_sync), config.disableSurfaceSync, onDisableSurfaceSyncChange)
                            ToggleRow(stringResource(R.string.settings_core_shader_compilation_notice), config.showCompileShaders, onShowShaderNoticeChange)
                            ToggleRow(stringResource(R.string.settings_core_performance_overlay), config.performanceOverlay, onPerformanceOverlayChange)
                            ChoiceChipRow(
                                title = stringResource(R.string.settings_core_overlay_detail_title),
                                selected = config.performanceOverlayDetail,
                                options = listOf(
                                    0 to stringResource(R.string.settings_core_overlay_detail_minimum),
                                    1 to stringResource(R.string.settings_core_overlay_detail_low),
                                    2 to stringResource(R.string.settings_core_overlay_detail_medium),
                                    3 to stringResource(R.string.settings_core_overlay_detail_maximum)
                                ),
                                onSelected = onPerformanceDetailChange
                            )
                            ChoiceChipRow(
                                title = stringResource(R.string.settings_core_overlay_position_title),
                                selected = config.performanceOverlayPosition,
                                options = listOf(
                                    0 to stringResource(R.string.settings_core_overlay_position_top_left),
                                    1 to stringResource(R.string.settings_core_overlay_position_top_center),
                                    2 to stringResource(R.string.settings_core_overlay_position_top_right),
                                    3 to stringResource(R.string.settings_core_overlay_position_bottom_left),
                                    4 to stringResource(R.string.settings_core_overlay_position_bottom_center),
                                    5 to stringResource(R.string.settings_core_overlay_position_bottom_right)
                                ),
                                onSelected = onPerformancePositionChange
                            )
                            ToggleRow(stringResource(R.string.settings_stretch_display_area), config.stretchDisplayArea, onStretchDisplayChange)
                            ToggleRow(stringResource(R.string.settings_vsync), config.vSync, onVsyncChange)
                        }
                    }

                    EmulationMenuTab.System -> {
                        MenuSectionCard(
                            title = stringResource(R.string.emulation_tab_system),
                            subtitle = stringResource(R.string.emulation_menu_next_launch)
                        ) {
                            ToggleRow(stringResource(R.string.settings_core_fps_hack), config.fpsHack, onFpsHackChange)
                            ToggleRow(stringResource(R.string.settings_pstv_mode), config.pstvMode, onPstvModeChange)
                            ToggleRow(stringResource(R.string.settings_core_touchpad_cursor), config.showTouchpadCursor, onTouchpadCursorChange)
                            ToggleRow(stringResource(R.string.settings_show_info_bar), config.showInfoBar, onInfoBarChange)
                            ToggleRow(stringResource(R.string.settings_turbo_mode), config.turboMode, onTurboModeChange)
                        }
                    }
                }

            }
        }

        Spacer(modifier = Modifier.width(12.dp))

        Surface(
            modifier = Modifier.fillMaxHeight().width(74.dp),
            shape = RoundedCornerShape(24.dp),
            color = EmulationPanelSoft,
            border = BorderStroke(1.dp, EmulationPanelBorder)
        ) {
            Column(
                modifier = Modifier.fillMaxSize().padding(vertical = 14.dp, horizontal = 10.dp),
                horizontalAlignment = Alignment.CenterHorizontally,
                verticalArrangement = Arrangement.spacedBy(10.dp)
            ) {
                EmulationRailButton(Icons.Rounded.Menu, activeTab == EmulationMenuTab.Session) { onTabSelected(EmulationMenuTab.Session) }
                EmulationRailButton(Icons.Rounded.Gamepad, activeTab == EmulationMenuTab.Overlay) { onTabSelected(EmulationMenuTab.Overlay) }
                EmulationRailButton(Icons.Rounded.SettingsSuggest, activeTab == EmulationMenuTab.Graphics) { onTabSelected(EmulationMenuTab.Graphics) }
                EmulationRailButton(Icons.Rounded.Refresh, activeTab == EmulationMenuTab.System) { onTabSelected(EmulationMenuTab.System) }
                Spacer(modifier = Modifier.weight(1f))
                EmulationRailButton(Icons.AutoMirrored.Rounded.ExitToApp, false, destructive = true, onClick = onClose)
            }
        }
    }
}

@Composable
private fun EmulationRailButton(
    icon: androidx.compose.ui.graphics.vector.ImageVector,
    selected: Boolean,
    destructive: Boolean = false,
    onClick: () -> Unit
) {
    Surface(
        modifier = Modifier.size(46.dp),
        shape = RoundedCornerShape(16.dp),
        color = when {
            destructive -> MaterialTheme.colorScheme.error.copy(alpha = 0.14f)
            selected -> MaterialTheme.colorScheme.primary.copy(alpha = 0.18f)
            else -> Color.White.copy(alpha = 0.04f)
        },
        border = BorderStroke(
            1.dp,
            when {
                destructive -> MaterialTheme.colorScheme.error.copy(alpha = 0.28f)
                selected -> MaterialTheme.colorScheme.primary.copy(alpha = 0.28f)
                else -> MaterialTheme.colorScheme.onSurface.copy(alpha = 0.06f)
            }
        ),
        onClick = onClick
    ) {
        Box(contentAlignment = Alignment.Center) {
            Icon(
                imageVector = icon,
                contentDescription = null,
                tint = when {
                    destructive -> MaterialTheme.colorScheme.error
                    selected -> MaterialTheme.colorScheme.primary
                    else -> EmulationTextSecondary
                }
            )
        }
    }
}

@Composable
private fun MenuSectionCard(title: String, subtitle: String, content: @Composable () -> Unit) {
    Surface(
        modifier = Modifier.fillMaxWidth(),
        shape = RoundedCornerShape(24.dp),
        color = Color.White.copy(alpha = 0.04f),
        border = BorderStroke(1.dp, Color.White.copy(alpha = 0.05f))
    ) {
        Column(modifier = Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(14.dp)) {
            Column(verticalArrangement = Arrangement.spacedBy(4.dp)) {
                Text(text = title, style = MaterialTheme.typography.titleMedium.copy(fontWeight = FontWeight.Bold), color = EmulationTextPrimary)
                Text(text = subtitle, color = EmulationTextSecondary, style = MaterialTheme.typography.bodySmall)
            }
            content()
        }
    }
}

@Composable
private fun MenuActionButton(
    icon: androidx.compose.ui.graphics.vector.ImageVector,
    title: String,
    subtitle: String,
    onClick: () -> Unit,
    containerColor: Color = Color.White.copy(alpha = 0.04f)
) {
    Surface(
        modifier = Modifier.fillMaxWidth().clickable(onClick = onClick),
        color = containerColor,
        shape = RoundedCornerShape(22.dp),
        border = BorderStroke(1.dp, Color.White.copy(alpha = 0.05f))
    ) {
        Row(
            modifier = Modifier.padding(horizontal = 16.dp, vertical = 14.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(14.dp)
        ) {
            Box(
                modifier = Modifier.size(42.dp).clip(CircleShape).background(MaterialTheme.colorScheme.primary.copy(alpha = 0.16f)),
                contentAlignment = Alignment.Center
            ) {
                Icon(icon, contentDescription = null, tint = MaterialTheme.colorScheme.primary)
            }
            Column(verticalArrangement = Arrangement.spacedBy(2.dp)) {
                Text(title, style = MaterialTheme.typography.titleSmall.copy(fontWeight = FontWeight.Bold), color = EmulationTextPrimary)
                Text(subtitle, style = MaterialTheme.typography.bodySmall, color = EmulationTextSecondary)
            }
        }
    }
}

@Composable
private fun ToggleRow(title: String, checked: Boolean, onCheckedChange: (Boolean) -> Unit) {
    Row(modifier = Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
        Text(text = title, modifier = Modifier.weight(1f), style = MaterialTheme.typography.bodyLarge, color = EmulationTextPrimary)
        Switch(checked = checked, onCheckedChange = onCheckedChange)
    }
}

@Composable
private fun SliderRow(
    title: String,
    value: Float,
    valueText: String,
    valueRange: ClosedFloatingPointRange<Float>,
    steps: Int,
    onValueChange: (Float) -> Unit
) {
    Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
        Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween, verticalAlignment = Alignment.CenterVertically) {
            Text(title, style = MaterialTheme.typography.bodyLarge, color = EmulationTextPrimary)
            Text(valueText, style = MaterialTheme.typography.labelMedium, color = MaterialTheme.colorScheme.primary)
        }
        Slider(value = value, onValueChange = onValueChange, valueRange = valueRange, steps = steps)
    }
}

@Composable
private fun ChoiceChipRow(
    title: String,
    selected: Int,
    options: List<Pair<Int, String>>,
    onSelected: (Int) -> Unit
) {
    Column(verticalArrangement = Arrangement.spacedBy(10.dp)) {
        Text(title, style = MaterialTheme.typography.bodyLarge, color = EmulationTextPrimary)
        FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
            options.forEach { (value, label) ->
                FilterChip(
                    selected = selected == value,
                    onClick = { onSelected(value) },
                    colors = androidx.compose.material3.FilterChipDefaults.filterChipColors(
                        containerColor = Color.White.copy(alpha = 0.04f),
                        labelColor = EmulationTextSecondary,
                        selectedContainerColor = MaterialTheme.colorScheme.primary.copy(alpha = 0.22f),
                        selectedLabelColor = EmulationTextPrimary
                    ),
                    label = { Text(label) }
                )
            }
        }
    }
}

@SuppressLint("ConfigurationScreenWidthHeight")
@Composable
private fun OnScreenControls(
    overlayScale: Float,
    overlayOpacity: Int,
    showTouchSwitch: Boolean,
    backTouchEnabled: Boolean,
    onBackTouchToggle: () -> Unit,
    onButtonChange: (Int, Boolean) -> Unit,
    onAxisChange: (Int, Short) -> Unit,
    modifier: Modifier = Modifier
) {
    val configuration = LocalConfiguration.current
    val isLandscape = configuration.screenWidthDp > configuration.screenHeightDp
    val cutoutInsets = WindowInsets.displayCutout.asPaddingValues()
    val navInsets = WindowInsets.navigationBars.asPaddingValues()
    val topInset = maxOf(cutoutInsets.calculateTopPadding(), WindowInsets.statusBars.asPaddingValues().calculateTopPadding())
    val bottomInset = navInsets.calculateBottomPadding()
    val sideInset = maxOf(
        cutoutInsets.calculateLeftPadding(androidx.compose.ui.unit.LayoutDirection.Ltr),
        cutoutInsets.calculateRightPadding(androidx.compose.ui.unit.LayoutDirection.Ltr)
    )

    val alpha = overlayOpacity / 100f
    val actionClusterSize = ((if (isLandscape) 142 else 160) * overlayScale).dp
    val dpadClusterSize = ((if (isLandscape) 136 else 154) * overlayScale).dp
    val analogSize = ((if (isLandscape) 112 else 126) * overlayScale).dp
    val shoulderWidth = ((if (isLandscape) 66 else 72) * overlayScale).dp
    val shoulderHeight = ((if (isLandscape) 32 else 36) * overlayScale).dp
    val centerWidth = ((if (isLandscape) 60 else 68) * overlayScale).dp
    val centerHeight = ((if (isLandscape) 26 else 30) * overlayScale).dp
    val wideCenterWidth = centerWidth * 1.2f
    val sidePadding = sideInset + if (isLandscape) 28.dp else 12.dp
    val bottomPadding = bottomInset + if (isLandscape) 24.dp else 36.dp
    val shoulderTopPadding = maxOf(40.dp, topInset + 4.dp)
    val centerGap = if (isLandscape) 10.dp * overlayScale else 12.dp * overlayScale
    val centerBottomPadding = bottomPadding - 6.dp
    val clusterSpacing = (if (isLandscape) 14.dp else 18.dp) * overlayScale
    val faceClusterDrop = (if (isLandscape) 18.dp else 14.dp) * overlayScale
    val leftClusterWidth = dpadClusterSize + analogSize + clusterSpacing
    val leftClusterHeight = maxOf(dpadClusterSize + faceClusterDrop, analogSize) + analogSize + clusterSpacing
    val rightClusterWidth = actionClusterSize + analogSize + clusterSpacing
    val rightClusterHeight = maxOf(actionClusterSize + faceClusterDrop, analogSize) + analogSize + clusterSpacing

    Box(modifier = modifier.fillMaxSize()) {
        TouchButtonGroup(
            specs = listOf(
                TouchButtonSpec("l2", R.drawable.button_l2, shoulderWidth, shoulderHeight, 0.dp, 0.dp, RoundedCornerShape(10.dp)) { onButtonChange(InputOverlay.ControlId.l2, it) },
                TouchButtonSpec("l1", R.drawable.button_l, shoulderWidth, shoulderHeight, 0.dp, 40.dp, RoundedCornerShape(10.dp)) { onButtonChange(InputOverlay.ControlId.l1, it) }
            ),
            alpha = alpha,
            modifier = Modifier.align(Alignment.TopStart).padding(top = shoulderTopPadding, start = sidePadding)
        )
        TouchButtonGroup(
            specs = listOf(
                TouchButtonSpec("r2", R.drawable.button_r2, shoulderWidth, shoulderHeight, 0.dp, 0.dp, RoundedCornerShape(10.dp)) { onButtonChange(InputOverlay.ControlId.r2, it) },
                TouchButtonSpec("r1", R.drawable.button_r, shoulderWidth, shoulderHeight, 0.dp, 40.dp, RoundedCornerShape(10.dp)) { onButtonChange(InputOverlay.ControlId.r1, it) }
            ),
            alpha = alpha,
            modifier = Modifier.align(Alignment.TopEnd).padding(top = shoulderTopPadding, end = sidePadding)
        )

        Box(
            modifier = Modifier
                .align(Alignment.BottomStart)
                .padding(start = sidePadding, bottom = bottomPadding)
                .size(leftClusterWidth, leftClusterHeight)
        ) {
            DpadCluster(
                clusterSize = dpadClusterSize,
                alpha = alpha,
                onButtonChange = onButtonChange,
                modifier = Modifier
                    .align(Alignment.TopStart)
                    .offset(y = faceClusterDrop)
            )
            AnalogStick(
                modifier = Modifier.align(Alignment.BottomEnd),
                analogSize = analogSize,
                alpha = alpha,
                onAxisChange = { x, y ->
                    onAxisChange(InputOverlay.ControlId.axis_left_x, x)
                    onAxisChange(InputOverlay.ControlId.axis_left_y, y)
                }
            )
        }
        Box(
            modifier = Modifier
                .align(Alignment.BottomEnd)
                .padding(end = sidePadding, bottom = bottomPadding)
                .size(rightClusterWidth, rightClusterHeight)
        ) {
            ActionCluster(
                clusterSize = actionClusterSize,
                alpha = alpha,
                onButtonChange = onButtonChange,
                modifier = Modifier
                    .align(Alignment.TopEnd)
                    .offset(y = faceClusterDrop)
            )
            AnalogStick(
                modifier = Modifier.align(Alignment.BottomStart),
                analogSize = analogSize,
                alpha = alpha,
                onAxisChange = { x, y ->
                    onAxisChange(InputOverlay.ControlId.axis_right_x, x)
                    onAxisChange(InputOverlay.ControlId.axis_right_y, y)
                }
            )
        }
        TouchButtonGroup(
            specs = buildList {
                add(TouchButtonSpec("select", R.drawable.button_select, wideCenterWidth, centerHeight, 0.dp, 0.dp, RoundedCornerShape(8.dp)) { onButtonChange(InputOverlay.ControlId.select, it) })
                add(TouchButtonSpec("ps", R.drawable.button_ps, centerHeight, centerHeight, wideCenterWidth + centerGap, 0.dp, CircleShape, onPressChange = { onButtonChange(InputOverlay.ControlId.guide, it) }))
                add(TouchButtonSpec("start", R.drawable.button_start, wideCenterWidth, centerHeight, wideCenterWidth + centerGap + centerHeight + centerGap, 0.dp, RoundedCornerShape(8.dp)) { onButtonChange(InputOverlay.ControlId.start, it) })
            },
            alpha = alpha,
            modifier = Modifier.align(Alignment.BottomCenter).padding(bottom = centerBottomPadding)
        )

        if (showTouchSwitch) {
            TouchButtonGroup(
                specs = listOf(
                    TouchButtonSpec(
                        "touch",
                        if (backTouchEnabled) R.drawable.button_touch_b else R.drawable.button_touch_f,
                        centerWidth * 1.2f,
                        centerHeight * 1.2f,
                        0.dp,
                        0.dp,
                        RoundedCornerShape(8.dp),
                        onClick = onBackTouchToggle
                    )
                ),
                alpha = alpha,
                modifier = Modifier.align(Alignment.BottomCenter).offset(y = (-84).dp)
            )
        }
    }
}

private data class TouchButtonSpec(
    val id: String,
    val drawableRes: Int,
    val width: Dp,
    val height: Dp,
    val x: Dp,
    val y: Dp,
    val shape: Shape,
    val onClick: (() -> Unit)? = null,
    val onPressChange: ((Boolean) -> Unit)? = null
)

@SuppressLint("ConfigurationScreenWidthHeight")
@Composable
private fun DpadCluster(clusterSize: Dp, alpha: Float, onButtonChange: (Int, Boolean) -> Unit, modifier: Modifier = Modifier) {
    val isLandscape = LocalConfiguration.current.screenWidthDp > LocalConfiguration.current.screenHeightDp
    val btn = clusterSize / 2.7f
    val gap = if (isLandscape) 16.dp else 18.dp
    val step = btn + gap
    val extent = step + btn
    val centerOffset = (extent - btn) / 2f
    TouchButtonGroup(
        specs = listOf(
            TouchButtonSpec("up", R.drawable.ic_controller_up_button, btn, btn, centerOffset, 0.dp, RoundedCornerShape(8.dp)) { onButtonChange(InputOverlay.ControlId.dup, it) },
            TouchButtonSpec("down", R.drawable.ic_controller_down_button, btn, btn, centerOffset, step, RoundedCornerShape(8.dp)) { onButtonChange(InputOverlay.ControlId.ddown, it) },
            TouchButtonSpec("left", R.drawable.ic_controller_left_button, btn, btn, 0.dp, centerOffset, RoundedCornerShape(8.dp)) { onButtonChange(InputOverlay.ControlId.dleft, it) },
            TouchButtonSpec("right", R.drawable.ic_controller_right_button, btn, btn, step, centerOffset, RoundedCornerShape(8.dp)) { onButtonChange(InputOverlay.ControlId.dright, it) }
        ),
        alpha = alpha,
        modifier = modifier
    )
}

@SuppressLint("ConfigurationScreenWidthHeight")
@Composable
private fun ActionCluster(clusterSize: Dp, alpha: Float, onButtonChange: (Int, Boolean) -> Unit, modifier: Modifier = Modifier) {
    val isLandscape = LocalConfiguration.current.screenWidthDp > LocalConfiguration.current.screenHeightDp
    val btn = clusterSize / 3.1f
    val gap = if (isLandscape) 24.dp else 28.dp
    val step = btn + gap
    val extent = step + btn
    val centerOffset = (extent - btn) / 2f
    TouchButtonGroup(
        specs = listOf(
            TouchButtonSpec("triangle", R.drawable.button_triangle, btn, btn, centerOffset, 0.dp, CircleShape) { onButtonChange(InputOverlay.ControlId.y, it) },
            TouchButtonSpec("cross", R.drawable.button_cross, btn, btn, centerOffset, step, CircleShape) { onButtonChange(InputOverlay.ControlId.a, it) },
            TouchButtonSpec("square", R.drawable.button_square, btn, btn, 0.dp, centerOffset, CircleShape) { onButtonChange(InputOverlay.ControlId.x, it) },
            TouchButtonSpec("circle", R.drawable.button_circle, btn, btn, step, centerOffset, CircleShape) { onButtonChange(InputOverlay.ControlId.b, it) }
        ),
        alpha = alpha,
        modifier = modifier
    )
}

@Composable
private fun TouchButtonGroup(
    specs: List<TouchButtonSpec>,
    alpha: Float,
    modifier: Modifier = Modifier,
    rotations: Map<String, Float> = emptyMap()
) {
    val density = LocalDensity.current
    val activeTargets = remember { mutableStateOf(setOf<String>()) }
    val downTargets = remember { mutableMapOf<Int, String?>() }
    val rects = remember(specs, density) {
        with(density) {
            specs.associate { spec ->
                spec.id to Rect(spec.x.toPx(), spec.y.toPx(), spec.x.toPx() + spec.width.toPx(), spec.y.toPx() + spec.height.toPx())
            }
        }
    }
    val groupRect = remember(rects) {
        Rect(
            left = rects.values.minOfOrNull { it.left } ?: 0f,
            top = rects.values.minOfOrNull { it.top } ?: 0f,
            right = rects.values.maxOfOrNull { it.right } ?: 0f,
            bottom = rects.values.maxOfOrNull { it.bottom } ?: 0f
        )
    }
    fun hitTarget(x: Float, y: Float): String? = specs.lastOrNull { rects.getValue(it.id).contains(Offset(x, y)) }?.id

    Box(
        modifier = modifier
            .offset { IntOffset(groupRect.left.roundToInt(), groupRect.top.roundToInt()) }
            .size(with(density) { (groupRect.right - groupRect.left).toDp() }, with(density) { (groupRect.bottom - groupRect.top).toDp() })
            .pointerInteropFilter { event ->
                when (event.actionMasked) {
                    MotionEvent.ACTION_DOWN, MotionEvent.ACTION_POINTER_DOWN -> {
                        val index = event.actionIndex
                        val pointerId = event.getPointerId(index)
                        val target = hitTarget(event.getX(index) + groupRect.left, event.getY(index) + groupRect.top)
                        downTargets[pointerId] = target
                        activeTargets.value = activeTargets.value + listOfNotNull(target)
                        target?.let { id -> specs.firstOrNull { it.id == id }?.onPressChange?.invoke(true) }
                        target != null
                    }
                    MotionEvent.ACTION_UP, MotionEvent.ACTION_POINTER_UP -> {
                        val index = event.actionIndex
                        val pointerId = event.getPointerId(index)
                        val target = downTargets.remove(pointerId)
                        target?.let { id ->
                            specs.firstOrNull { it.id == id }?.onPressChange?.invoke(false)
                            specs.firstOrNull { it.id == id }?.onClick?.invoke()
                            activeTargets.value = activeTargets.value - id
                        }
                        target != null
                    }
                    MotionEvent.ACTION_CANCEL -> {
                        activeTargets.value.forEach { id -> specs.firstOrNull { it.id == id }?.onPressChange?.invoke(false) }
                        activeTargets.value = emptySet()
                        downTargets.clear()
                        true
                    }
                    else -> false
                }
            }
    ) {
        specs.forEach { spec ->
            AssetButton(
                drawableRes = spec.drawableRes,
                width = spec.width,
                height = spec.height,
                alpha = alpha,
                shape = spec.shape,
                pressed = activeTargets.value.contains(spec.id),
                rotation = rotations[spec.id] ?: 0f,
                modifier = Modifier.offset {
                    IntOffset(
                        (spec.x.roundToPx() - groupRect.left.roundToInt()),
                        (spec.y.roundToPx() - groupRect.top.roundToInt())
                    )
                }
            )
        }
    }
}

@Composable
private fun AssetButton(
    drawableRes: Int,
    width: Dp,
    height: Dp,
    alpha: Float,
    shape: Shape,
    pressed: Boolean,
    modifier: Modifier = Modifier,
    rotation: Float = 0f
) {
    val scale by animateFloatAsState(targetValue = if (pressed) 0.94f else 1f, animationSpec = tween(80), label = "overlay_asset_scale")
    Box(
        modifier = modifier
            .size(width = width, height = height)
            .graphicsLayer(alpha = alpha, rotationZ = rotation, scaleX = scale, scaleY = scale)
            .clip(shape),
        contentAlignment = Alignment.Center
    ) {
        Image(
            painter = painterResource(drawableRes),
            contentDescription = null,
            modifier = Modifier.fillMaxSize(),
            contentScale = ContentScale.Fit
        )
    }
}

@Composable
private fun AnalogStick(
    analogSize: Dp,
    alpha: Float,
    onAxisChange: (Short, Short) -> Unit,
    modifier: Modifier = Modifier
) {
    var sizePx by remember { mutableStateOf(androidx.compose.ui.geometry.Size.Zero) }
    var thumbOffset by remember { mutableStateOf(Offset.Zero) }
    var lastX by remember { mutableIntStateOf(0) }
    var lastY by remember { mutableIntStateOf(0) }

    fun sendAxis(x: Float, y: Float) {
        val quantizedX = (x * Short.MAX_VALUE).roundToInt().coerceIn(Short.MIN_VALUE.toInt(), Short.MAX_VALUE.toInt())
        val quantizedY = (y * Short.MAX_VALUE).roundToInt().coerceIn(Short.MIN_VALUE.toInt(), Short.MAX_VALUE.toInt())
        if (quantizedX == lastX && quantizedY == lastY) return
        lastX = quantizedX
        lastY = quantizedY
        onAxisChange(quantizedX.toShort(), quantizedY.toShort())
    }

    fun resetStick() {
        thumbOffset = Offset.Zero
        sendAxis(0f, 0f)
    }

    Box(
        modifier = modifier
            .size(analogSize)
            .graphicsLayer(alpha = alpha)
            .onSizeChanged { sizePx = androidx.compose.ui.geometry.Size(it.width.toFloat(), it.height.toFloat()) }
            .pointerInput(sizePx) {
                detectDragGestures(
                    onDragStart = { offset ->
                        if (sizePx.width == 0f) return@detectDragGestures
                        val center = Offset(sizePx.width / 2f, sizePx.height / 2f)
                        val maxDistance = minOf(sizePx.width, sizePx.height) * 0.28f
                        val raw = offset - center
                        val distance = raw.getDistance()
                        val clamped = if (distance > maxDistance && distance > 0f) raw * (maxDistance / distance) else raw
                        thumbOffset = clamped
                        sendAxis((clamped.x / maxDistance).coerceIn(-1f, 1f), (clamped.y / maxDistance).coerceIn(-1f, 1f))
                    },
                    onDragEnd = { resetStick() },
                    onDragCancel = { resetStick() }
                ) { change, _ ->
                    change.consume()
                    if (sizePx.width == 0f) return@detectDragGestures
                    val center = Offset(sizePx.width / 2f, sizePx.height / 2f)
                    val maxDistance = minOf(sizePx.width, sizePx.height) * 0.28f
                    val raw = change.position - center
                    val distance = raw.getDistance()
                    val clamped = if (distance > maxDistance && distance > 0f) raw * (maxDistance / distance) else raw
                    thumbOffset = clamped
                    val nx = (clamped.x / maxDistance).coerceIn(-1f, 1f).let { if (abs(it) < 0.12f) 0f else it }
                    val ny = (clamped.y / maxDistance).coerceIn(-1f, 1f).let { if (abs(it) < 0.12f) 0f else it }
                    sendAxis(nx, ny)
                }
            },
        contentAlignment = Alignment.Center
    ) {
        Image(
            painter = painterResource(R.drawable.joystick_range),
            contentDescription = null,
            modifier = Modifier.fillMaxSize(),
            contentScale = ContentScale.Fit
        )
        Image(
            painter = painterResource(R.drawable.joystick),
            contentDescription = null,
            modifier = Modifier.size(analogSize * 0.56f).offset { IntOffset(thumbOffset.x.roundToInt(), thumbOffset.y.roundToInt()) },
            contentScale = ContentScale.Fit
        )
    }
}

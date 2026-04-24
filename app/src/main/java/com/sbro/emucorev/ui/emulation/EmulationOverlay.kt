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
import androidx.compose.foundation.layout.BoxWithConstraints
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
import androidx.compose.runtime.rememberUpdatedState
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
    val controlLayoutRepository = remember(activity) { TouchControlLayoutRepository(activity) }
    val overlayBridge = remember(activity) { activity.getmOverlay() }
    var config by remember(activity) { mutableStateOf(repository.load()) }
    var controlLayout by remember(activity) { mutableStateOf(controlLayoutRepository.load()) }
    var controlsEditMode by remember { mutableStateOf(false) }
    var menuOpen by remember { mutableStateOf(false) }
    var menuButtonVisible by remember { mutableStateOf(true) }
    var pausedByMenu by remember { mutableStateOf(false) }
    var activeTab by remember { mutableStateOf(EmulationMenuTab.Session) }
    var backTouchEnabled by remember { mutableStateOf(false) }
    var exitDialogVisible by remember { mutableStateOf(false) }
    val gameId = remember(activity) { activity.currentGameIdOrIntent() }
    val hasPhysicalGamepad = activity.hasPhysicalGamepad
    val showTouchControls = !menuOpen &&
        (
            controlsEditMode ||
                (
                    config.enableGamepadOverlay &&
                        overlayBridge.effectiveOverlayMask != 0 &&
                        !hasPhysicalGamepad
                    )
            )

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

    LaunchedEffect(menuOpen, controlsEditMode) {
        val shouldPauseNative = menuOpen || controlsEditMode
        if (shouldPauseNative && !pausedByMenu) {
            activity.setMenuPaused(true)
            pausedByMenu = true
        } else if (!shouldPauseNative && pausedByMenu) {
            activity.setMenuPaused(false)
            pausedByMenu = false
        }
        if (menuOpen || controlsEditMode) {
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
            } else if (controlsEditMode) {
                controlsEditMode = false
                overlayBridge.setIsInEditMode(false)
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
                editMode = controlsEditMode,
                savedLayout = controlLayout,
                onLayoutChange = { updated ->
                    controlLayout = updated
                    controlLayoutRepository.save(updated)
                },
                onEditDone = {
                    controlsEditMode = false
                    overlayBridge.setIsInEditMode(false)
                },
                onEditReset = {
                    controlLayoutRepository.reset()
                    controlLayout = null
                    persistConfig { it.copy(overlayScale = 0.9f, overlayOpacity = 100) }
                },
                onBackTouchToggle = {
                    backTouchEnabled = !backTouchEnabled
                    overlayBridge.setTouchState(backTouchEnabled)
                },
                onButtonChange = { button, pressed -> overlayBridge.setButton(button, pressed) },
                onAxisChange = { axis, value -> overlayBridge.setAxis(axis, value) }
            )
        }

        AnimatedVisibility(
            visible = !controlsEditMode && (menuButtonVisible || menuOpen),
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
                onEditControls = {
                    persistConfig { it.copy(enableGamepadOverlay = true) }
                    menuOpen = false
                    menuButtonVisible = true
                    controlsEditMode = true
                    overlayBridge.setIsInEditMode(true)
                },
                onResetOverlay = {
                    controlLayoutRepository.reset()
                    controlLayout = null
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
    onEditControls: () -> Unit,
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
                            icon = Icons.Rounded.Gamepad,
                            title = stringResource(R.string.emulation_menu_edit_controls),
                            subtitle = stringResource(R.string.emulation_menu_edit_controls_desc),
                            onClick = onEditControls
                        )
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
    editMode: Boolean,
    savedLayout: List<TouchControlElement>?,
    onLayoutChange: (List<TouchControlElement>) -> Unit,
    onEditDone: () -> Unit,
    onEditReset: () -> Unit,
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
    val sidePadding = sideInset + if (isLandscape) 28.dp else 12.dp
    val bottomPadding = bottomInset + if (isLandscape) 24.dp else 36.dp
    val shoulderTopPadding = maxOf(40.dp, topInset + 4.dp)

    BoxWithConstraints(modifier = modifier.fillMaxSize()) {
        val density = LocalDensity.current
        val canvasWidth = with(density) { maxWidth.toPx() }.coerceAtLeast(1f)
        val canvasHeight = with(density) { maxHeight.toPx() }.coerceAtLeast(1f)
        val defaultLayout = remember(
            canvasWidth,
            canvasHeight,
            isLandscape,
            overlayScale,
            sidePadding,
            bottomPadding,
            shoulderTopPadding
        ) {
            buildDefaultTouchLayout(
                canvasWidth = canvasWidth,
                canvasHeight = canvasHeight,
                isLandscape = isLandscape,
                overlayScale = overlayScale,
                density = density.density,
                sidePaddingPx = with(density) { sidePadding.toPx() },
                bottomPaddingPx = with(density) { bottomPadding.toPx() },
                shoulderTopPaddingPx = with(density) { shoulderTopPadding.toPx() }
            )
        }
        val mergedLayout = remember(defaultLayout, savedLayout) { mergeTouchLayout(defaultLayout, savedLayout) }
        var controls by remember(defaultLayout) { mutableStateOf(mergedLayout) }
        var selectedId by remember(editMode) { mutableStateOf<String?>(null) }
        LaunchedEffect(mergedLayout) {
            controls = mergedLayout
        }
        val selected = controls.firstOrNull { it.id == selectedId } ?: controls.firstOrNull()
        val selectedIndex = selected?.let { controls.indexOfFirst { element -> element.id == it.id } } ?: -1
        val selectedDescriptor = selected?.id?.let(::touchControlDescriptor)
        val defaultSelected = selected?.id?.let { id -> defaultLayout.firstOrNull { it.id == id } }
        val selectedScalePercent = if (selected != null && defaultSelected != null) {
            val currentSize = maxOf(selected.width * canvasWidth, selected.height * canvasHeight)
            val defaultSize = maxOf(defaultSelected.width * canvasWidth, defaultSelected.height * canvasHeight).coerceAtLeast(1f)
            ((currentSize / defaultSize) * 100f).roundToInt().coerceIn(25, 300)
        } else {
            100
        }

        fun commitLayoutChange(transform: (List<TouchControlElement>) -> List<TouchControlElement>) {
            val updated = transform(controls).map { it.coerceToCanvas() }
            controls = updated
            onLayoutChange(updated)
        }

        fun updateSelectedSize(percentDelta: Int) {
            val selectedElement = selected ?: return
            val target = controls.firstOrNull { it.id == selectedElement.id } ?: selectedElement
            val baseline = defaultLayout.firstOrNull { it.id == target.id } ?: target
            val currentSize = maxOf(target.width * canvasWidth, target.height * canvasHeight)
            val defaultSize = maxOf(baseline.width * canvasWidth, baseline.height * canvasHeight).coerceAtLeast(1f)
            val currentPercent = ((currentSize / defaultSize) * 100f).roundToInt().coerceIn(25, 300)
            val nextPercent = (currentPercent + percentDelta).coerceIn(35, 250) / 100f
            val nextWidth = (baseline.width * nextPercent).coerceIn(0.035f, 0.5f)
            val nextHeight = (baseline.height * nextPercent).coerceIn(0.035f, 0.5f)
            commitLayoutChange { currentControls ->
                currentControls.replaceElement(
                    target.copy(
                        width = nextWidth,
                        height = nextHeight,
                        x = target.x.coerceIn(0f, 1f - nextWidth),
                        y = target.y.coerceIn(0f, 1f - nextHeight)
                    )
                )
            }
        }

        fun selectNext() {
            if (controls.isEmpty()) return
            val nextIndex = if (selectedIndex < 0) 0 else (selectedIndex + 1) % controls.size
            selectedId = controls[nextIndex].id
        }

        controls.forEach { element ->
            val descriptor = touchControlDescriptor(element.id) ?: return@forEach
            if (!editMode && (!element.visible || (element.id == TouchControlIds.TOUCH && !showTouchSwitch))) {
                return@forEach
            }
            TouchControlCanvasItem(
                element = element,
                descriptor = descriptor,
                canvasWidth = canvasWidth,
                canvasHeight = canvasHeight,
                alpha = if (editMode && !element.visible) 0.28f else alpha,
                selected = editMode && selected?.id == element.id,
                editMode = editMode,
                backTouchEnabled = backTouchEnabled,
                onSelected = { selectedId = element.id },
                onElementChange = { updated -> commitLayoutChange { currentControls -> currentControls.replaceElement(updated) } },
                onBackTouchToggle = onBackTouchToggle,
                onButtonChange = onButtonChange,
                onAxisChange = onAxisChange
            )
        }

        if (editMode && selected != null && selectedDescriptor != null) {
            TouchControlEditorChrome(
                selectedLabel = selectedDescriptor.label,
                selectedVisible = selected.visible,
                selectedScalePercent = selectedScalePercent,
                onSelectNext = ::selectNext,
                onReset = onEditReset,
                onVisibilityToggle = {
                    val currentSelected = controls.firstOrNull { it.id == selected.id } ?: selected
                    commitLayoutChange { currentControls ->
                        currentControls.replaceElement(currentSelected.copy(visible = !currentSelected.visible))
                    }
                },
                onSizeDecrease = { updateSelectedSize(-10) },
                onSizeIncrease = { updateSelectedSize(10) },
                onDone = onEditDone,
                modifier = Modifier.align(Alignment.TopCenter)
            )
        }
    }
}

private enum class TouchControlType {
    Button,
    Analog,
    TouchSwitch
}

private data class TouchControlDescriptor(
    val id: String,
    val label: String,
    val drawableRes: Int,
    val shape: Shape,
    val type: TouchControlType,
    val controlId: Int? = null,
    val axisX: Int? = null,
    val axisY: Int? = null
)

private fun touchControlDescriptor(id: String): TouchControlDescriptor? = when (id) {
    TouchControlIds.L2 -> TouchControlDescriptor(id, "L2", R.drawable.button_l2, RoundedCornerShape(10.dp), TouchControlType.Button, InputOverlay.ControlId.l2)
    TouchControlIds.L1 -> TouchControlDescriptor(id, "L1", R.drawable.button_l, RoundedCornerShape(10.dp), TouchControlType.Button, InputOverlay.ControlId.l1)
    TouchControlIds.R2 -> TouchControlDescriptor(id, "R2", R.drawable.button_r2, RoundedCornerShape(10.dp), TouchControlType.Button, InputOverlay.ControlId.r2)
    TouchControlIds.R1 -> TouchControlDescriptor(id, "R1", R.drawable.button_r, RoundedCornerShape(10.dp), TouchControlType.Button, InputOverlay.ControlId.r1)
    TouchControlIds.DPAD_UP -> TouchControlDescriptor(id, "Up", R.drawable.ic_controller_up_button, RoundedCornerShape(8.dp), TouchControlType.Button, InputOverlay.ControlId.dup)
    TouchControlIds.DPAD_DOWN -> TouchControlDescriptor(id, "Down", R.drawable.ic_controller_down_button, RoundedCornerShape(8.dp), TouchControlType.Button, InputOverlay.ControlId.ddown)
    TouchControlIds.DPAD_LEFT -> TouchControlDescriptor(id, "Left", R.drawable.ic_controller_left_button, RoundedCornerShape(8.dp), TouchControlType.Button, InputOverlay.ControlId.dleft)
    TouchControlIds.DPAD_RIGHT -> TouchControlDescriptor(id, "Right", R.drawable.ic_controller_right_button, RoundedCornerShape(8.dp), TouchControlType.Button, InputOverlay.ControlId.dright)
    TouchControlIds.LEFT_STICK -> TouchControlDescriptor(id, "Left stick", R.drawable.joystick_range, CircleShape, TouchControlType.Analog, axisX = InputOverlay.ControlId.axis_left_x, axisY = InputOverlay.ControlId.axis_left_y)
    TouchControlIds.RIGHT_STICK -> TouchControlDescriptor(id, "Right stick", R.drawable.joystick_range, CircleShape, TouchControlType.Analog, axisX = InputOverlay.ControlId.axis_right_x, axisY = InputOverlay.ControlId.axis_right_y)
    TouchControlIds.TRIANGLE -> TouchControlDescriptor(id, "Triangle", R.drawable.button_triangle, CircleShape, TouchControlType.Button, InputOverlay.ControlId.y)
    TouchControlIds.CROSS -> TouchControlDescriptor(id, "Cross", R.drawable.button_cross, CircleShape, TouchControlType.Button, InputOverlay.ControlId.a)
    TouchControlIds.SQUARE -> TouchControlDescriptor(id, "Square", R.drawable.button_square, CircleShape, TouchControlType.Button, InputOverlay.ControlId.x)
    TouchControlIds.CIRCLE -> TouchControlDescriptor(id, "Circle", R.drawable.button_circle, CircleShape, TouchControlType.Button, InputOverlay.ControlId.b)
    TouchControlIds.SELECT -> TouchControlDescriptor(id, "Select", R.drawable.button_select, RoundedCornerShape(8.dp), TouchControlType.Button, InputOverlay.ControlId.select)
    TouchControlIds.PS -> TouchControlDescriptor(id, "PS", R.drawable.button_ps, CircleShape, TouchControlType.Button, InputOverlay.ControlId.guide)
    TouchControlIds.START -> TouchControlDescriptor(id, "Start", R.drawable.button_start, RoundedCornerShape(8.dp), TouchControlType.Button, InputOverlay.ControlId.start)
    TouchControlIds.TOUCH -> TouchControlDescriptor(id, "Touch", R.drawable.button_touch_f, RoundedCornerShape(8.dp), TouchControlType.TouchSwitch)
    else -> null
}

@Composable
private fun TouchControlCanvasItem(
    element: TouchControlElement,
    descriptor: TouchControlDescriptor,
    canvasWidth: Float,
    canvasHeight: Float,
    alpha: Float,
    selected: Boolean,
    editMode: Boolean,
    backTouchEnabled: Boolean,
    onSelected: () -> Unit,
    onElementChange: (TouchControlElement) -> Unit,
    onBackTouchToggle: () -> Unit,
    onButtonChange: (Int, Boolean) -> Unit,
    onAxisChange: (Int, Short) -> Unit
) {
    val density = LocalDensity.current
    val latestElement by rememberUpdatedState(element)
    val xPx = element.x * canvasWidth
    val yPx = element.y * canvasHeight
    val widthPx = element.width * canvasWidth
    val heightPx = element.height * canvasHeight
    val sizeModifier = Modifier
        .offset { IntOffset(xPx.roundToInt(), yPx.roundToInt()) }
        .size(width = with(density) { widthPx.toDp() }, height = with(density) { heightPx.toDp() })

    val inputModifier = if (editMode) {
        Modifier
            .clickable(
                interactionSource = remember { MutableInteractionSource() },
                indication = null,
                onClick = onSelected
            )
            .pointerInput(element.id, canvasWidth, canvasHeight) {
                var draggedElement = latestElement
                detectDragGestures(
                    onDragStart = {
                        draggedElement = latestElement
                        onSelected()
                    }
                ) { change, dragAmount ->
                    change.consume()
                    draggedElement = draggedElement.copy(
                        x = (draggedElement.x + dragAmount.x / canvasWidth).coerceIn(0f, 1f - draggedElement.width),
                        y = (draggedElement.y + dragAmount.y / canvasHeight).coerceIn(0f, 1f - draggedElement.height)
                    )
                    onElementChange(draggedElement)
                }
            }
            .border(
                width = if (selected) 2.dp else 1.dp,
                color = if (selected) MaterialTheme.colorScheme.primary else Color.White.copy(alpha = 0.34f),
                shape = descriptor.shape
            )
    } else {
        when (descriptor.type) {
            TouchControlType.Button -> Modifier.pointerInteropFilter { event ->
                val controlId = descriptor.controlId ?: return@pointerInteropFilter false
                when (event.actionMasked) {
                    MotionEvent.ACTION_DOWN, MotionEvent.ACTION_POINTER_DOWN -> {
                        onButtonChange(controlId, true)
                        true
                    }

                    MotionEvent.ACTION_UP, MotionEvent.ACTION_POINTER_UP, MotionEvent.ACTION_CANCEL -> {
                        onButtonChange(controlId, false)
                        true
                    }

                    else -> true
                }
            }

            TouchControlType.TouchSwitch -> Modifier.pointerInteropFilter { event ->
                when (event.actionMasked) {
                    MotionEvent.ACTION_UP -> {
                        onBackTouchToggle()
                        true
                    }

                    else -> true
                }
            }

            TouchControlType.Analog -> Modifier
        }
    }

    Box(modifier = sizeModifier.then(inputModifier), contentAlignment = Alignment.Center) {
        when (descriptor.type) {
            TouchControlType.Analog -> {
                if (editMode) {
                    StaticAnalogStick(alpha = alpha)
                } else {
                    AnalogStick(
                        analogSize = with(density) { minOf(widthPx, heightPx).toDp() },
                        alpha = alpha,
                        onAxisChange = { x, y ->
                            descriptor.axisX?.let { onAxisChange(it, x) }
                            descriptor.axisY?.let { onAxisChange(it, y) }
                        }
                    )
                }
            }

            TouchControlType.Button,
            TouchControlType.TouchSwitch -> {
                AssetButton(
                    drawableRes = if (descriptor.type == TouchControlType.TouchSwitch && backTouchEnabled) {
                        R.drawable.button_touch_b
                    } else {
                        descriptor.drawableRes
                    },
                    width = with(density) { widthPx.toDp() },
                    height = with(density) { heightPx.toDp() },
                    alpha = alpha,
                    shape = descriptor.shape,
                    pressed = false
                )
            }
        }
    }
}

@Composable
private fun StaticAnalogStick(alpha: Float) {
    Box(
        modifier = Modifier.fillMaxSize().graphicsLayer(alpha = alpha),
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
            modifier = Modifier.fillMaxSize(0.56f),
            contentScale = ContentScale.Fit
        )
    }
}

@Composable
private fun TouchControlEditorChrome(
    selectedLabel: String,
    selectedVisible: Boolean,
    selectedScalePercent: Int,
    onSelectNext: () -> Unit,
    onReset: () -> Unit,
    onVisibilityToggle: () -> Unit,
    onSizeDecrease: () -> Unit,
    onSizeIncrease: () -> Unit,
    onDone: () -> Unit,
    modifier: Modifier = Modifier
) {
    Column(
        modifier = modifier
            .fillMaxWidth()
            .padding(top = 8.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.spacedBy(6.dp)
    ) {
        Surface(
            shape = RoundedCornerShape(18.dp),
            color = MaterialTheme.colorScheme.primary.copy(alpha = 0.82f),
            border = BorderStroke(1.dp, Color.White.copy(alpha = 0.08f))
        ) {
            Column(
                modifier = Modifier.padding(horizontal = 22.dp, vertical = 7.dp),
                horizontalAlignment = Alignment.CenterHorizontally
            ) {
                Text(
                    text = stringResource(R.string.emulation_controls_editor_title),
                    style = MaterialTheme.typography.titleMedium.copy(fontWeight = FontWeight.Bold),
                    color = Color.White
                )
                Text(
                    text = selectedLabel,
                    style = MaterialTheme.typography.labelLarge.copy(fontWeight = FontWeight.SemiBold),
                    color = Color.White.copy(alpha = 0.78f)
                )
            }
        }

        Row(
            horizontalArrangement = Arrangement.spacedBy(10.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            EditorToolbarButton(
                label = selectedLabel,
                onClick = onSelectNext,
                minWidth = 86.dp
            )
            EditorIconButton(onClick = onReset) {
                Icon(Icons.Rounded.Refresh, contentDescription = null, tint = Color.White)
            }
            EditorIconButton(onClick = onVisibilityToggle, enabled = selectedVisible) {
                Icon(
                    imageVector = if (selectedVisible) Icons.Rounded.Visibility else Icons.Rounded.VisibilityOff,
                    contentDescription = null,
                    tint = Color.White.copy(alpha = if (selectedVisible) 0.95f else 0.58f)
                )
            }
            EditorToolbarButton(
                label = stringResource(R.string.emulation_controls_editor_done),
                onClick = onDone,
                containerColor = MaterialTheme.colorScheme.primary,
                minWidth = 82.dp
            )
        }

        Surface(
            shape = RoundedCornerShape(18.dp),
            color = Color(0xFF171B27).copy(alpha = 0.94f),
            border = BorderStroke(1.dp, Color.White.copy(alpha = 0.06f))
        ) {
            Row(
                modifier = Modifier.padding(horizontal = 10.dp, vertical = 8.dp),
                horizontalArrangement = Arrangement.spacedBy(10.dp),
                verticalAlignment = Alignment.CenterVertically
            ) {
                EditorSizeButton("-", onClick = onSizeDecrease)
                Text(
                    text = stringResource(R.string.emulation_controls_editor_percent, selectedScalePercent),
                    style = MaterialTheme.typography.titleMedium.copy(fontWeight = FontWeight.Bold),
                    color = Color.White,
                    modifier = Modifier.width(60.dp),
                    textAlign = androidx.compose.ui.text.style.TextAlign.Center
                )
                EditorSizeButton("+", onClick = onSizeIncrease)
            }
        }
    }
}

@Composable
private fun EditorToolbarButton(
    label: String,
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
    containerColor: Color = Color(0xFF17171D).copy(alpha = 0.94f),
    minWidth: Dp = 74.dp
) {
    Surface(
        modifier = modifier
            .width(minWidth)
            .height(42.dp),
        shape = RoundedCornerShape(14.dp),
        color = containerColor,
        border = BorderStroke(1.dp, Color.White.copy(alpha = 0.08f)),
        onClick = onClick
    ) {
        Box(contentAlignment = Alignment.Center) {
            Text(
                text = label,
                style = MaterialTheme.typography.titleSmall.copy(fontWeight = FontWeight.Bold),
                color = Color.White
            )
        }
    }
}

@Composable
private fun EditorIconButton(
    onClick: () -> Unit,
    enabled: Boolean = true,
    content: @Composable () -> Unit
) {
    Surface(
        modifier = Modifier.size(width = 54.dp, height = 42.dp),
        shape = RoundedCornerShape(14.dp),
        color = Color(0xFF17171D).copy(alpha = if (enabled) 0.94f else 0.54f),
        border = BorderStroke(1.dp, Color.White.copy(alpha = if (enabled) 0.08f else 0.03f)),
        onClick = onClick
    ) {
        Box(contentAlignment = Alignment.Center) {
            content()
        }
    }
}

@Composable
private fun EditorSizeButton(
    label: String,
    onClick: () -> Unit
) {
    Surface(
        modifier = Modifier.size(width = 62.dp, height = 40.dp),
        shape = RoundedCornerShape(13.dp),
        color = Color(0xFF252A36),
        border = BorderStroke(1.dp, Color.White.copy(alpha = 0.05f)),
        onClick = onClick
    ) {
        Box(contentAlignment = Alignment.Center) {
            Text(
                text = label,
                style = MaterialTheme.typography.titleLarge.copy(fontWeight = FontWeight.Bold),
                color = Color.White
            )
        }
    }
}

private fun mergeTouchLayout(
    defaults: List<TouchControlElement>,
    saved: List<TouchControlElement>?
): List<TouchControlElement> {
    val savedById = saved.orEmpty().associateBy { it.id }
    return defaults.map { default -> savedById[default.id]?.coerceToCanvas() ?: default }
}

private fun List<TouchControlElement>.replaceElement(updated: TouchControlElement): List<TouchControlElement> {
    return map { element -> if (element.id == updated.id) updated.coerceToCanvas() else element }
}

private fun TouchControlElement.coerceToCanvas(): TouchControlElement {
    val safeWidth = width.coerceIn(0.035f, 0.5f)
    val safeHeight = height.coerceIn(0.035f, 0.5f)
    return copy(
        width = safeWidth,
        height = safeHeight,
        x = x.coerceIn(0f, 1f - safeWidth),
        y = y.coerceIn(0f, 1f - safeHeight)
    )
}

private fun buildDefaultTouchLayout(
    canvasWidth: Float,
    canvasHeight: Float,
    isLandscape: Boolean,
    overlayScale: Float,
    density: Float,
    sidePaddingPx: Float,
    bottomPaddingPx: Float,
    shoulderTopPaddingPx: Float
): List<TouchControlElement> {
    fun dp(value: Float): Float = value * density
    fun element(id: String, x: Float, y: Float, width: Float, height: Float, visible: Boolean = true): TouchControlElement {
        return TouchControlElement(
            id = id,
            x = (x / canvasWidth).coerceIn(0f, 1f),
            y = (y / canvasHeight).coerceIn(0f, 1f),
            width = (width / canvasWidth).coerceIn(0.035f, 0.5f),
            height = (height / canvasHeight).coerceIn(0.035f, 0.5f),
            visible = visible
        ).coerceToCanvas()
    }

    val actionClusterSize = (if (isLandscape) 142f else 160f) * overlayScale * dp(1f)
    val dpadClusterSize = (if (isLandscape) 136f else 154f) * overlayScale * dp(1f)
    val analogSize = (if (isLandscape) 112f else 126f) * overlayScale * dp(1f)
    val shoulderWidth = (if (isLandscape) 66f else 72f) * overlayScale * dp(1f)
    val shoulderHeight = (if (isLandscape) 32f else 36f) * overlayScale * dp(1f)
    val centerWidth = (if (isLandscape) 60f else 68f) * overlayScale * dp(1f)
    val centerHeight = (if (isLandscape) 26f else 30f) * overlayScale * dp(1f)
    val wideCenterWidth = centerWidth * 1.2f
    val centerGap = (if (isLandscape) 10f else 12f) * overlayScale * dp(1f)
    val centerBottomPadding = bottomPaddingPx - dp(6f)
    val clusterSpacing = (if (isLandscape) 14f else 18f) * overlayScale * dp(1f)
    val faceClusterDrop = (if (isLandscape) 18f else 14f) * overlayScale * dp(1f)
    val leftClusterWidth = dpadClusterSize + analogSize + clusterSpacing
    val leftClusterHeight = maxOf(dpadClusterSize + faceClusterDrop, analogSize) + analogSize + clusterSpacing
    val rightClusterWidth = actionClusterSize + analogSize + clusterSpacing
    val rightClusterHeight = maxOf(actionClusterSize + faceClusterDrop, analogSize) + analogSize + clusterSpacing

    val dpadButton = dpadClusterSize / 2.7f
    val dpadGap = if (isLandscape) dp(16f) else dp(18f)
    val dpadStep = dpadButton + dpadGap
    val dpadExtent = dpadStep + dpadButton
    val dpadCenter = (dpadExtent - dpadButton) / 2f
    val dpadX = sidePaddingPx
    val dpadY = canvasHeight - bottomPaddingPx - leftClusterHeight + faceClusterDrop
    val leftAnalogX = sidePaddingPx + dpadClusterSize + clusterSpacing
    val leftAnalogY = canvasHeight - bottomPaddingPx - analogSize

    val actionButton = actionClusterSize / 3.1f
    val actionGap = if (isLandscape) dp(24f) else dp(28f)
    val actionStep = actionButton + actionGap
    val actionExtent = actionStep + actionButton
    val actionCenter = (actionExtent - actionButton) / 2f
    val rightGroupX = canvasWidth - sidePaddingPx - rightClusterWidth
    val actionX = rightGroupX + rightClusterWidth - actionClusterSize
    val actionY = canvasHeight - bottomPaddingPx - rightClusterHeight + faceClusterDrop
    val rightAnalogX = rightGroupX
    val rightAnalogY = canvasHeight - bottomPaddingPx - analogSize

    val centerGroupWidth = wideCenterWidth + centerGap + centerHeight + centerGap + wideCenterWidth
    val centerX = (canvasWidth - centerGroupWidth) / 2f
    val centerY = canvasHeight - centerBottomPadding - centerHeight
    val touchWidth = centerWidth * 1.2f
    val touchHeight = centerHeight * 1.2f

    return listOf(
        element(TouchControlIds.L2, sidePaddingPx, shoulderTopPaddingPx, shoulderWidth, shoulderHeight),
        element(TouchControlIds.L1, sidePaddingPx, shoulderTopPaddingPx + dp(40f), shoulderWidth, shoulderHeight),
        element(TouchControlIds.R2, canvasWidth - sidePaddingPx - shoulderWidth, shoulderTopPaddingPx, shoulderWidth, shoulderHeight),
        element(TouchControlIds.R1, canvasWidth - sidePaddingPx - shoulderWidth, shoulderTopPaddingPx + dp(40f), shoulderWidth, shoulderHeight),
        element(TouchControlIds.DPAD_UP, dpadX + dpadCenter, dpadY, dpadButton, dpadButton),
        element(TouchControlIds.DPAD_DOWN, dpadX + dpadCenter, dpadY + dpadStep, dpadButton, dpadButton),
        element(TouchControlIds.DPAD_LEFT, dpadX, dpadY + dpadCenter, dpadButton, dpadButton),
        element(TouchControlIds.DPAD_RIGHT, dpadX + dpadStep, dpadY + dpadCenter, dpadButton, dpadButton),
        element(TouchControlIds.LEFT_STICK, leftAnalogX, leftAnalogY, analogSize, analogSize),
        element(TouchControlIds.RIGHT_STICK, rightAnalogX, rightAnalogY, analogSize, analogSize),
        element(TouchControlIds.TRIANGLE, actionX + actionCenter, actionY, actionButton, actionButton),
        element(TouchControlIds.CROSS, actionX + actionCenter, actionY + actionStep, actionButton, actionButton),
        element(TouchControlIds.SQUARE, actionX, actionY + actionCenter, actionButton, actionButton),
        element(TouchControlIds.CIRCLE, actionX + actionStep, actionY + actionCenter, actionButton, actionButton),
        element(TouchControlIds.SELECT, centerX, centerY, wideCenterWidth, centerHeight),
        element(TouchControlIds.PS, centerX + wideCenterWidth + centerGap, centerY, centerHeight, centerHeight),
        element(TouchControlIds.START, centerX + wideCenterWidth + centerGap + centerHeight + centerGap, centerY, wideCenterWidth, centerHeight),
        element(TouchControlIds.TOUCH, (canvasWidth - touchWidth) / 2f, canvasHeight - touchHeight - dp(84f), touchWidth, touchHeight)
    )
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

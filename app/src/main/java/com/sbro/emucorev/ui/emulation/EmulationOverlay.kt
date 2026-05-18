package com.sbro.emucorev.ui.emulation

import android.annotation.SuppressLint
import android.view.MotionEvent
import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.animation.core.tween
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.slideInHorizontally
import androidx.compose.animation.slideInVertically
import androidx.compose.animation.slideOutHorizontally
import androidx.compose.animation.slideOutVertically
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.gestures.detectDragGestures
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.asPaddingValues
import androidx.compose.foundation.layout.displayCutout
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.navigationBars
import androidx.compose.foundation.layout.offset
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.statusBars
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.Refresh
import androidx.compose.material.icons.rounded.Visibility
import androidx.compose.material.icons.rounded.VisibilityOff
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableLongStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Shape
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.input.pointer.pointerInteropFilter
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
import com.sbro.emucorev.R
import com.sbro.emucorev.core.VitaCoreConfig
import com.sbro.emucorev.core.VitaGameSettingsRepository
import com.sbro.emucorev.core.vita.Emulator
import com.sbro.emucorev.core.vita.overlay.InputOverlay
import com.sbro.emucorev.data.InstalledGameRepository
import com.sbro.emucorev.data.TrophyRepository
import com.sbro.emucorev.data.VitaTrophy
import com.sbro.emucorev.data.VitaTrophyGrade
import com.sbro.emucorev.ui.common.LocalImage
import kotlin.math.abs
import kotlin.math.roundToInt
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext

@SuppressLint("ConfigurationScreenWidthHeight")
@Composable
fun EmulationOverlayHost(
    activity: Emulator,
    modifier: Modifier = Modifier
) {
    val gameId = remember(activity) { activity.currentGameIdOrIntent() }
    val repository = remember(activity) { VitaGameSettingsRepository(activity) }
    val trophyRepository = remember(activity) { TrophyRepository() }
    val controlLayoutRepository = remember(activity) { TouchControlLayoutRepository(activity) }
    val overlayBridge = remember(activity) { activity.getmOverlay() }
    var config by remember(activity, gameId) { mutableStateOf(repository.loadEffective(gameId)) }
    var controlLayout by remember(activity) { mutableStateOf(controlLayoutRepository.load()) }
    var controlsEditMode by remember { mutableStateOf(false) }
    var menuOpen by remember { mutableStateOf(false) }
    var menuButtonVisible by remember { mutableStateOf(true) }
    var userPaused by remember { mutableStateOf(false) }
    var backTouchEnabled by remember { mutableStateOf(false) }
    var exitDialogVisible by remember { mutableStateOf(false) }
    var trophyNotification by remember { mutableStateOf<VitaTrophy?>(null) }
    var trophyNotificationQueue by remember { mutableStateOf<List<VitaTrophy>>(emptyList()) }
    var sessionElapsedMs by remember(activity) { mutableLongStateOf(activity.currentPlayTimeElapsedMs()) }
    val gameTitle = remember(activity, gameId) {
        val installedTitle = InstalledGameRepository().findByTitleId(activity, gameId)
            ?.title
            ?.takeIf { it.isNotBlank() && !it.equals(gameId, ignoreCase = true) }
        installedTitle
            ?: activity.getRunningGameTitle().takeIf { it.isNotBlank() && !it.equals(gameId, ignoreCase = true) }
            ?: gameId
    }
    val hasPhysicalGamepad = activity.hasPhysicalGamepad
    val touchControlsActive = controlsEditMode || (config.enableGamepadOverlay && !hasPhysicalGamepad)
    val showTouchControls = !menuOpen &&
        (
            controlsEditMode ||
                (
                    touchControlsActive &&
                        overlayBridge.effectiveOverlayMask != 0
                )
            )
    val effectivePaused = userPaused || menuOpen || controlsEditMode

    fun persistConfig(transform: (VitaCoreConfig) -> VitaCoreConfig) {
        config = transform(config)
        repository.savePreservingDriverOverride(gameId, config)
    }

    fun syncPerformanceOverlayState() {
        activity.setPerformanceOverlayState(
            config.performanceOverlay,
            config.performanceOverlayDetail,
            config.performanceOverlayPosition
        )
    }

    fun applyRuntimeCoreSettings() {
        activity.applyRuntimeCoreSettings(
            config.vSync,
            config.stretchDisplayArea,
            config.disableSurfaceSync,
            config.fpsHack,
            config.turboMode,
            config.showCompileShaders,
            config.pstvMode
        )
    }

    DisposableEffect(config) {
        overlayBridge.synchronizeConfig(config)
        syncPerformanceOverlayState()
        applyRuntimeCoreSettings()
        onDispose {}
    }

    LaunchedEffect(effectivePaused) {
        activity.setMenuPaused(effectivePaused)
    }

    LaunchedEffect(touchControlsActive) {
        overlayBridge.setTouchControlsActive(touchControlsActive)
    }

    LaunchedEffect(showTouchControls) {
        if (showTouchControls) {
            repeat(8) {
                overlayBridge.ensureControllerAttached()
                kotlinx.coroutines.delay(350)
            }
        }
    }

    LaunchedEffect(menuOpen, controlsEditMode) {
        if (menuOpen || controlsEditMode) {
            menuButtonVisible = true
        }
    }

    LaunchedEffect(activity) {
        while (true) {
            sessionElapsedMs = activity.currentPlayTimeElapsedMs()
            kotlinx.coroutines.delay(1_000)
        }
    }

    LaunchedEffect(activity, gameId) {
        var knownUnlockedKeys = withContext(Dispatchers.IO) {
            trophyRepository.loadForTitle(activity, gameId)
                .flatMap { set -> set.trophies.map { trophy -> "${set.communicationId}:${trophy.id}" to trophy } }
                .filter { it.second.unlocked }
                .map { it.first }
                .toSet()
        }
        while (true) {
            kotlinx.coroutines.delay(1_500)
            val unlocked = withContext(Dispatchers.IO) {
                trophyRepository.loadForTitle(activity, gameId)
                    .flatMap { set -> set.trophies.map { trophy -> "${set.communicationId}:${trophy.id}" to trophy } }
                    .filter { it.second.unlocked }
                    .sortedWith(compareBy<Pair<String, VitaTrophy>> { it.second.unlockedAtEpochSeconds ?: Long.MAX_VALUE }.thenBy { it.second.id })
            }
            val fresh = unlocked.filter { it.first !in knownUnlockedKeys }
            if (fresh.isNotEmpty()) {
                knownUnlockedKeys = knownUnlockedKeys + fresh.map { it.first }
                trophyNotificationQueue = trophyNotificationQueue + fresh.map { it.second }
            }
        }
    }

    LaunchedEffect(trophyNotification, trophyNotificationQueue) {
        if (trophyNotification == null && trophyNotificationQueue.isNotEmpty()) {
            val next = trophyNotificationQueue.first()
            trophyNotificationQueue = trophyNotificationQueue.drop(1)
            trophyNotification = next
        }
    }

    LaunchedEffect(trophyNotification) {
        val visibleTrophy = trophyNotification ?: return@LaunchedEffect
        kotlinx.coroutines.delay(5_000)
        if (trophyNotification == visibleTrophy) {
            trophyNotification = null
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

        val configuration = LocalConfiguration.current
        val useSidePanel = configuration.screenWidthDp > configuration.screenHeightDp

        val menuCallbacks = EmulationMenuCallbacks(
            onPauseToggle = {
                if (effectivePaused) {
                    userPaused = false
                    menuOpen = false
                    if (controlsEditMode) {
                        controlsEditMode = false
                        overlayBridge.setIsInEditMode(false)
                    }
                } else {
                    userPaused = true
                    menuButtonVisible = true
                }
            },
            onExit = { exitDialogVisible = true },
            onEditControls = {
                persistConfig { it.copy(enableGamepadOverlay = true) }
                menuOpen = false
                menuButtonVisible = true
                controlsEditMode = true
                overlayBridge.setIsInEditMode(true)
            },
            onControlsVisibility = {
                persistConfig { it.copy(enableGamepadOverlay = !it.enableGamepadOverlay) }
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
            onTouchSwitch = { enabled ->
                persistConfig { it.copy(overlayShowTouchSwitch = enabled) }
                if (!enabled) {
                    backTouchEnabled = false
                    overlayBridge.setTouchState(false)
                }
            },
            onOverlayScale = { value -> persistConfig { it.copy(overlayScale = value) } },
            onOverlayOpacity = { value -> persistConfig { it.copy(overlayOpacity = value) } },
            onPerformanceOverlay = { enabled ->
                persistConfig { it.copy(performanceOverlay = enabled) }
                syncPerformanceOverlayState()
            },
            onPerformanceDetail = { value ->
                persistConfig { it.copy(performanceOverlayDetail = value) }
                syncPerformanceOverlayState()
            },
            onPerformancePosition = { value ->
                persistConfig { it.copy(performanceOverlayPosition = value) }
                syncPerformanceOverlayState()
            },
            onAudioVolume = { volume ->
                persistConfig { it.copy(audioVolume = volume) }
                activity.setAudioVolume(volume)
            },
            onBgmVolume = { volume -> persistConfig { it.copy(bgmVolume = volume) } },
            onInfoBar = { enabled -> persistConfig { it.copy(showInfoBar = enabled) } },
            onTouchpadCursor = { enabled -> persistConfig { it.copy(showTouchpadCursor = enabled) } },
            onResolutionMultiplier = { value -> persistConfig { it.copy(resolutionMultiplier = value) } },
            onVsync = { enabled -> persistConfig { it.copy(vSync = enabled) } },
            onStretchDisplay = { enabled -> persistConfig { it.copy(stretchDisplayArea = enabled) } },
            onHighAccuracy = { enabled -> persistConfig { it.copy(highAccuracy = enabled) } },
            onFpsHack = { enabled -> persistConfig { it.copy(fpsHack = enabled) } },
            onTurboMode = { enabled -> persistConfig { it.copy(turboMode = enabled) } },
            onDisableSurfaceSync = { enabled -> persistConfig { it.copy(disableSurfaceSync = enabled) } },
            onShowShaderNotice = { enabled -> persistConfig { it.copy(showCompileShaders = enabled) } },
            onPstvMode = { enabled -> persistConfig { it.copy(pstvMode = enabled) } },
            onShowWelcome = { enabled -> persistConfig { it.copy(showWelcome = enabled) } },
            onWarnMissingFirmware = { enabled -> persistConfig { it.copy(warnMissingFirmware = enabled) } },
            onGamepadDeadzone = { value -> persistConfig { it.copy(gamepadDeadzone = value) } },
            onGamepadAnalogMultiplier = { value -> persistConfig { it.copy(analogMultiplier = value) } },
            onGamepadTriggerThreshold = { value -> persistConfig { it.copy(gamepadTriggerThreshold = value) } },
            onGamepadButtonProfile = { value -> persistConfig { it.copy(gamepadButtonProfile = value) } },
            onGamepadVibration = { enabled -> persistConfig { it.copy(gamepadVibration = enabled) } },
            onGamepadSwapSticks = { enabled -> persistConfig { it.copy(gamepadSwapSticks = enabled) } },
            onGamepadInvertLeftY = { enabled -> persistConfig { it.copy(gamepadInvertLeftY = enabled) } },
            onGamepadInvertRightY = { enabled -> persistConfig { it.copy(gamepadInvertRightY = enabled) } }
        )

        AnimatedVisibility(
            visible = !controlsEditMode && menuButtonVisible,
            enter = fadeIn(tween(180)),
            exit = fadeOut(tween(140)),
            modifier = Modifier.align(Alignment.TopCenter)
        ) {
            EmulationQuickBar(
                paused = effectivePaused,
                onPauseToggle = menuCallbacks.onPauseToggle,
                onScreenshot = { activity.requestScreenshot() },
                onOpenMenu = {
                    menuOpen = !menuOpen
                    menuButtonVisible = true
                }
            )
        }

        AnimatedVisibility(
            visible = trophyNotification != null,
            enter = slideInVertically(initialOffsetY = { -it }) + fadeIn(tween(220)),
            exit = slideOutVertically(targetOffsetY = { -it }) + fadeOut(tween(180)),
            modifier = Modifier.align(Alignment.TopEnd)
        ) {
            trophyNotification?.let { trophy ->
                TrophyUnlockNotification(
                    trophy = trophy,
                    modifier = Modifier.padding(top = 18.dp, end = 18.dp)
                )
            }
        }

        AnimatedVisibility(visible = menuOpen, enter = fadeIn(tween(220)), exit = fadeOut(tween(180))) {
            Box(
                modifier = Modifier
                    .fillMaxSize()
                    .background(MaterialTheme.colorScheme.scrim.copy(alpha = 0.42f))
                    .clickable(interactionSource = remember { MutableInteractionSource() }, indication = null) {
                        menuOpen = false
                    }
            )
        }

        AnimatedVisibility(
            visible = menuOpen,
            enter = if (useSidePanel) {
                slideInHorizontally(initialOffsetX = { it }) + fadeIn(tween(220))
            } else {
                androidx.compose.animation.slideInVertically(initialOffsetY = { it }) + fadeIn(tween(220))
            },
            exit = if (useSidePanel) {
                slideOutHorizontally(targetOffsetX = { it }) + fadeOut(tween(180))
            } else {
                androidx.compose.animation.slideOutVertically(targetOffsetY = { it }) + fadeOut(tween(180))
            },
            modifier = Modifier.align(if (useSidePanel) Alignment.CenterEnd else Alignment.BottomCenter)
        ) {
            EmulationGameMenu(
                gameTitle = gameTitle,
                gameId = gameId,
                config = config,
                paused = effectivePaused,
                sessionElapsedMs = sessionElapsedMs,
                expandHorizontally = useSidePanel,
                physicalGamepadConnected = hasPhysicalGamepad,
                callbacks = menuCallbacks
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
        if (hasPhysicalGamepad && !controlsEditMode) {
            overlayBridge.setTouchControlsActive(false)
        }
    }
}

@Composable
private fun TrophyUnlockNotification(
    trophy: VitaTrophy,
    modifier: Modifier = Modifier
) {
    val gradeColor = when (trophy.grade) {
        VitaTrophyGrade.Platinum -> Color(0xFF7AD8FF)
        VitaTrophyGrade.Gold -> Color(0xFFE1AA28)
        VitaTrophyGrade.Silver -> Color(0xFFB9C1CB)
        VitaTrophyGrade.Bronze -> Color(0xFFB8794A)
        VitaTrophyGrade.Unknown -> MaterialTheme.colorScheme.primary
    }
    Surface(
        modifier = modifier.width(330.dp),
        shape = RoundedCornerShape(22.dp),
        color = MaterialTheme.colorScheme.surface.copy(alpha = 0.94f),
        tonalElevation = 8.dp,
        border = BorderStroke(1.dp, gradeColor.copy(alpha = 0.55f))
    ) {
        Row(
            modifier = Modifier.padding(12.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            Surface(
                modifier = Modifier.size(58.dp),
                shape = RoundedCornerShape(16.dp),
                color = gradeColor.copy(alpha = 0.14f),
                border = BorderStroke(1.dp, gradeColor.copy(alpha = 0.48f))
            ) {
                LocalImage(
                    path = trophy.iconPath,
                    contentDescription = trophy.name,
                    fallbackLabel = trophy.name.ifBlank { "T" },
                    modifier = Modifier.fillMaxSize()
                )
            }
            Column(
                modifier = Modifier.weight(1f),
                verticalArrangement = Arrangement.spacedBy(3.dp)
            ) {
                Text(
                    text = stringResource(R.string.achievements_unlocked_notification_title),
                    style = MaterialTheme.typography.labelLarge.copy(fontWeight = FontWeight.Bold),
                    color = gradeColor,
                    maxLines = 1
                )
                Text(
                    text = trophy.name.ifBlank { stringResource(R.string.achievements_trophy) },
                    style = MaterialTheme.typography.titleSmall.copy(fontWeight = FontWeight.SemiBold),
                    color = MaterialTheme.colorScheme.onSurface,
                    maxLines = 1,
                    overflow = androidx.compose.ui.text.style.TextOverflow.Ellipsis
                )
                if (trophy.detail.isNotBlank()) {
                    Text(
                        text = trophy.detail,
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                        maxLines = 1,
                        overflow = androidx.compose.ui.text.style.TextOverflow.Ellipsis
                    )
                }
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

        if (editMode) {
            touchControlGroups.forEach { group ->
                val groupElements = controls.filter { it.id in group.ids }
                if (groupElements.size == group.ids.size) {
                    TouchControlGroupFrame(
                        group = group,
                        elements = groupElements,
                        canvasWidth = canvasWidth,
                        canvasHeight = canvasHeight,
                        onDragStart = { selectedId = group.ids.firstOrNull() },
                        onGroupChange = { updatedElements ->
                            commitLayoutChange { currentControls ->
                                currentControls.replaceElements(updatedElements)
                            }
                        }
                    )
                }
            }
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

private data class TouchControlGroup(
    val ids: Set<String>
)

private data class TouchControlGroupBounds(
    val x: Float,
    val y: Float,
    val width: Float,
    val height: Float
)

private val touchControlGroups = listOf(
    TouchControlGroup(
        setOf(
            TouchControlIds.DPAD_UP,
            TouchControlIds.DPAD_DOWN,
            TouchControlIds.DPAD_LEFT,
            TouchControlIds.DPAD_RIGHT
        )
    ),
    TouchControlGroup(
        setOf(
            TouchControlIds.TRIANGLE,
            TouchControlIds.CROSS,
            TouchControlIds.SQUARE,
            TouchControlIds.CIRCLE
        )
    )
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
private fun TouchControlGroupFrame(
    group: TouchControlGroup,
    elements: List<TouchControlElement>,
    canvasWidth: Float,
    canvasHeight: Float,
    onDragStart: () -> Unit,
    onGroupChange: (List<TouchControlElement>) -> Unit
) {
    val density = LocalDensity.current
    val latestElements by rememberUpdatedState(elements)
    val bounds = elements.groupBounds()
    val paddingPx = with(density) { 14.dp.toPx() }
    val paddedX = (bounds.x * canvasWidth - paddingPx).coerceAtLeast(0f)
    val paddedY = (bounds.y * canvasHeight - paddingPx).coerceAtLeast(0f)
    val paddedRight = ((bounds.x + bounds.width) * canvasWidth + paddingPx).coerceAtMost(canvasWidth)
    val paddedBottom = ((bounds.y + bounds.height) * canvasHeight + paddingPx).coerceAtMost(canvasHeight)
    val widthPx = (paddedRight - paddedX).coerceAtLeast(1f)
    val heightPx = (paddedBottom - paddedY).coerceAtLeast(1f)

    Box(
        modifier = Modifier
            .offset { IntOffset(paddedX.roundToInt(), paddedY.roundToInt()) }
            .size(
                width = with(density) { widthPx.toDp() },
                height = with(density) { heightPx.toDp() }
            )
            .clip(RoundedCornerShape(18.dp))
            .background(MaterialTheme.colorScheme.primary.copy(alpha = 0.08f))
            .border(
                width = 1.dp,
                color = MaterialTheme.colorScheme.primary.copy(alpha = 0.42f),
                shape = RoundedCornerShape(18.dp)
            )
            .pointerInput(group.ids, canvasWidth, canvasHeight) {
                var draggedElements = latestElements
                detectDragGestures(
                    onDragStart = {
                        draggedElements = latestElements
                        onDragStart()
                    }
                ) { change, dragAmount ->
                    change.consume()
                    draggedElements = draggedElements.moveGroupBy(
                        dx = dragAmount.x / canvasWidth,
                        dy = dragAmount.y / canvasHeight
                    )
                    onGroupChange(draggedElements)
                }
            }
    )
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
    var pressed by remember(element.id, editMode) { mutableStateOf(false) }
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
                        pressed = true
                        onButtonChange(controlId, true)
                        true
                    }

                    MotionEvent.ACTION_UP, MotionEvent.ACTION_POINTER_UP, MotionEvent.ACTION_CANCEL -> {
                        pressed = false
                        onButtonChange(controlId, false)
                        true
                    }

                    else -> true
                }
            }

            TouchControlType.TouchSwitch -> Modifier.pointerInteropFilter { event ->
                when (event.actionMasked) {
                    MotionEvent.ACTION_DOWN, MotionEvent.ACTION_POINTER_DOWN -> {
                        pressed = true
                        true
                    }

                    MotionEvent.ACTION_UP -> {
                        pressed = false
                        onBackTouchToggle()
                        true
                    }

                    MotionEvent.ACTION_POINTER_UP, MotionEvent.ACTION_CANCEL -> {
                        pressed = false
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
                    pressed = !editMode && pressed
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

private fun List<TouchControlElement>.replaceElements(updated: List<TouchControlElement>): List<TouchControlElement> {
    val updatedById = updated.associateBy { it.id }
    return map { element -> updatedById[element.id]?.coerceToCanvas() ?: element }
}

private fun List<TouchControlElement>.groupBounds(): TouchControlGroupBounds {
    val left = minOf { it.x }
    val top = minOf { it.y }
    val right = maxOf { it.x + it.width }
    val bottom = maxOf { it.y + it.height }
    return TouchControlGroupBounds(
        x = left,
        y = top,
        width = right - left,
        height = bottom - top
    )
}

private fun List<TouchControlElement>.moveGroupBy(dx: Float, dy: Float): List<TouchControlElement> {
    if (isEmpty()) return this
    val bounds = groupBounds()
    val clampedDx = dx.coerceIn(-bounds.x, 1f - (bounds.x + bounds.width))
    val clampedDy = dy.coerceIn(-bounds.y, 1f - (bounds.y + bounds.height))
    if (clampedDx == 0f && clampedDy == 0f) return this
    return map { element ->
        element.copy(
            x = element.x + clampedDx,
            y = element.y + clampedDy
        ).coerceToCanvas()
    }
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
    val leftClusterHeight = maxOf(dpadClusterSize + faceClusterDrop, analogSize) + analogSize + clusterSpacing
    val rightClusterWidth = actionClusterSize + analogSize + clusterSpacing
    val rightClusterHeight = maxOf(actionClusterSize + faceClusterDrop, analogSize) + analogSize + clusterSpacing

    val dpadButton = dpadClusterSize / 2.7f
    val dpadGap = if (isLandscape) dp(16f) else dp(18f)
    val dpadStep = dpadButton + dpadGap
    val dpadExtent = dpadStep + dpadButton
    val dpadCenter = (dpadExtent - dpadButton) / 2f
    val dpadY = canvasHeight - bottomPaddingPx - leftClusterHeight + faceClusterDrop
    val leftAnalogX = sidePaddingPx + dpadClusterSize + clusterSpacing
    val leftAnalogY = canvasHeight - bottomPaddingPx - analogSize

    val actionButton = actionClusterSize / 3.1f
    val actionGap = if (isLandscape) dp(36f) else dp(42f)
    val actionStep = actionButton + actionGap
    val actionExtent = actionStep + actionButton
    val actionCenter = (actionExtent - actionButton) / 2f
    val rightGroupX = canvasWidth - sidePaddingPx - rightClusterWidth
    val actionX = rightGroupX + rightClusterWidth - actionClusterSize
    val actionY = canvasHeight - bottomPaddingPx - rightClusterHeight + faceClusterDrop
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
        element(TouchControlIds.DPAD_UP, sidePaddingPx + dpadCenter, dpadY, dpadButton, dpadButton),
        element(TouchControlIds.DPAD_DOWN, sidePaddingPx + dpadCenter, dpadY + dpadStep, dpadButton, dpadButton),
        element(TouchControlIds.DPAD_LEFT, sidePaddingPx, dpadY + dpadCenter, dpadButton, dpadButton),
        element(TouchControlIds.DPAD_RIGHT, sidePaddingPx + dpadStep, dpadY + dpadCenter, dpadButton, dpadButton),
        element(TouchControlIds.LEFT_STICK, leftAnalogX, leftAnalogY, analogSize, analogSize),
        element(TouchControlIds.RIGHT_STICK, rightGroupX, rightAnalogY, analogSize, analogSize),
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
    val scale by animateFloatAsState(targetValue = if (pressed) 1.5f else 1f, animationSpec = tween(80), label = "overlay_asset_scale")
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

    fun updateStick(position: Offset) {
        if (sizePx.width == 0f || sizePx.height == 0f) return
        val center = Offset(sizePx.width / 2f, sizePx.height / 2f)
        val maxDistance = minOf(sizePx.width, sizePx.height) * 0.48f
        val raw = position - center
        val distance = raw.getDistance()
        val clamped = if (distance > maxDistance && distance > 0f) raw * (maxDistance / distance) else raw
        thumbOffset = clamped
        val nx = (clamped.x / maxDistance).coerceIn(-1f, 1f).let { if (abs(it) < 0.12f) 0f else it }
        val ny = (clamped.y / maxDistance).coerceIn(-1f, 1f).let { if (abs(it) < 0.12f) 0f else it }
        sendAxis(nx, ny)
    }

    Box(
        modifier = modifier
            .size(analogSize)
            .graphicsLayer(alpha = alpha)
            .onSizeChanged { sizePx = androidx.compose.ui.geometry.Size(it.width.toFloat(), it.height.toFloat()) }
            .pointerInput(sizePx) {
                detectDragGestures(
                    onDragStart = { offset ->
                        updateStick(offset)
                    },
                    onDragEnd = { resetStick() },
                    onDragCancel = { resetStick() }
                ) { change, _ ->
                    change.consume()
                    updateStick(change.position)
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

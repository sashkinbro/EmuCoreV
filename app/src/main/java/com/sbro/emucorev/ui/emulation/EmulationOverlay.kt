@file:OptIn(androidx.compose.foundation.layout.ExperimentalLayoutApi::class)

package com.sbro.emucorev.ui.emulation

import android.annotation.SuppressLint
import android.provider.OpenableColumns
import android.util.Log
import android.view.MotionEvent
import android.widget.Toast
import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.core.tween
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.slideInHorizontally
import androidx.compose.animation.slideOutHorizontally
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.gestures.detectDragGestures
import androidx.compose.foundation.gestures.detectTapGestures
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
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.foundation.layout.navigationBars
import androidx.compose.foundation.layout.offset
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.statusBarsIgnoringVisibility
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import com.sbro.emucorev.ui.theme.neon.neonShape
import com.sbro.emucorev.ui.theme.neon.LocalNeonTheme

import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.Refresh
import androidx.compose.material.icons.rounded.TouchApp
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
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.key
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableLongStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.ColorFilter
import androidx.compose.ui.graphics.Shape
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.input.pointer.pointerInteropFilter
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.layout.onSizeChanged
import androidx.compose.ui.platform.LocalConfiguration
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.platform.LocalView
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.IntOffset
import androidx.compose.ui.unit.dp
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.LifecycleEventObserver
import androidx.lifecycle.compose.LocalLifecycleOwner
import com.sbro.emucorev.R
import com.sbro.emucorev.BuildConfig
import com.sbro.emucorev.core.AndroidGyroscopeInput
import com.sbro.emucorev.core.AndroidTouchHaptics
import com.sbro.emucorev.core.AndroidTouchHaptics.ButtonPhase
import com.sbro.emucorev.core.CHEATS_ENABLED
import com.sbro.emucorev.core.CheatBridge
import com.sbro.emucorev.core.SaveStateResult
import com.sbro.emucorev.core.SaveStateRepository
import com.sbro.emucorev.core.SaveStateSlot
import com.sbro.emucorev.core.VitaCheatSnapshot
import com.sbro.emucorev.core.VitaCoreConfig
import com.sbro.emucorev.core.VitaCoreConfigRepository
import com.sbro.emucorev.core.VitaGameSettingsRepository
import com.sbro.emucorev.core.vita.Emulator
import com.sbro.emucorev.core.vita.overlay.InputOverlay
import com.sbro.emucorev.data.InstalledGameRepository
import com.sbro.emucorev.data.CustomizationPreferences
import com.sbro.emucorev.data.TouchControlPressEffect
import com.sbro.emucorev.data.TouchControlVisualStyle
import com.sbro.emucorev.ui.common.VectorAnalogStick
import com.sbro.emucorev.ui.common.VectorOverlayButton
import java.io.File
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import kotlin.math.abs
import kotlin.math.roundToInt

private sealed interface SaveStateAction {
    val slot: Int

    data class Save(override val slot: Int, val overwrite: Boolean) : SaveStateAction
    data class Load(override val slot: Int) : SaveStateAction
    data class Delete(override val slot: Int) : SaveStateAction
}

@SuppressLint("ConfigurationScreenWidthHeight")
@Composable
fun EmulationOverlayHost(
    activity: Emulator,
    modifier: Modifier = Modifier
) {
    // Read currentGameId as Compose State — recomposes automatically when the
    // native core calls setCurrentGameId() after the SDL surface is ready.
    // Fall back to the intent-carried ID until the native callback fires.
    val nativeGameId = activity.currentGameId
    val gameId = nativeGameId.ifBlank { activity.currentGameIdOrIntent() }
    val launchStateLoading = activity.launchStateLoading
    val repository = remember(activity) { VitaGameSettingsRepository(activity) }
    val coreConfigRepository = remember(activity) { VitaCoreConfigRepository(activity) }
    val controlLayoutRepository = remember(activity) { TouchControlLayoutRepository(activity) }
    val customizationPreferences = remember(activity) { CustomizationPreferences(activity) }
    val customization by customizationPreferences.settings.collectAsState()
    val overlayBridge = remember(activity) { activity.getmOverlay() }

    // A finger can be lifted while the controls are being hidden (menu opens,
    // IME shows, activity pauses); Android then never delivers ACTION_UP to the
    // stick and the axis would stay where it was. Zero both sticks on every
    // such edge so the game never keeps a direction pressed.
    fun resetOverlayAxes() {
        overlayBridge.sendAxis(InputOverlay.ControlId.axis_left_x, 0.toShort())
        overlayBridge.sendAxis(InputOverlay.ControlId.axis_left_y, 0.toShort())
        overlayBridge.sendAxis(InputOverlay.ControlId.axis_right_x, 0.toShort())
        overlayBridge.sendAxis(InputOverlay.ControlId.axis_right_y, 0.toShort())
    }
    var config by remember(activity, gameId) { mutableStateOf(repository.loadEffective(gameId)) }
    var controlLayout by remember(activity) { mutableStateOf(controlLayoutRepository.load()) }
    var controlsEditMode by remember { mutableStateOf(false) }
    var menuOpen by remember { mutableStateOf(false) }
    var menuButtonVisible by remember { mutableStateOf(true) }
    var userPaused by remember { mutableStateOf(false) }
    var touchMode by remember { mutableIntStateOf(0) }
    var exitDialogVisible by remember { mutableStateOf(false) }
    var cheatSnapshot by remember(activity) { mutableStateOf(VitaCheatSnapshot.EMPTY) }
    var sessionElapsedMs by remember(activity) { mutableLongStateOf(activity.currentPlayTimeElapsedMs()) }
    val installedGame = remember(activity, gameId) {
        InstalledGameRepository().findByTitleId(activity, gameId)
    }
    val gameTitle = remember(activity, gameId, installedGame) {
        installedGame?.title
            ?.takeIf { it.isNotBlank() && !it.equals(gameId, ignoreCase = true) }
            ?: activity.getRunningGameTitle().takeIf { it.isNotBlank() && !it.equals(gameId, ignoreCase = true) }
            ?: gameId
    }
    val gameIconPath = installedGame?.iconPath
    val hasPhysicalGamepad = activity.hasPhysicalGamepad
    val nativeImeActive = activity.nativeKeyboardRequested && activity.nativeImeState?.active == true
    var showControlsWithGamepad by remember { mutableStateOf(false) }
    val controlsSuppressedByGamepad = hasPhysicalGamepad && !showControlsWithGamepad
    val controlsVisible = config.enableGamepadOverlay && !controlsSuppressedByGamepad
    val touchControlsActive = !nativeImeActive && (controlsEditMode || (config.enableGamepadOverlay && !controlsSuppressedByGamepad))
    val showTouchControls = !nativeImeActive && !menuOpen &&
        (
            controlsEditMode ||
                (
                    touchControlsActive &&
                        overlayBridge.effectiveOverlayMask != 0
                )
            )
    val effectivePaused = userPaused || menuOpen || controlsEditMode
    val lifecycleOwner = LocalLifecycleOwner.current
    var inputResumed by remember(lifecycleOwner) {
        mutableStateOf(lifecycleOwner.lifecycle.currentState.isAtLeast(Lifecycle.State.RESUMED))
    }
    DisposableEffect(lifecycleOwner) {
        val observer = LifecycleEventObserver { _, event ->
            inputResumed = lifecycleOwner.lifecycle.currentState.isAtLeast(Lifecycle.State.RESUMED)
            if (event == Lifecycle.Event.ON_PAUSE || event == Lifecycle.Event.ON_STOP) {
                resetOverlayAxes()
            }
        }
        lifecycleOwner.lifecycle.addObserver(observer)
        onDispose { lifecycleOwner.lifecycle.removeObserver(observer) }
    }
    LaunchedEffect(showTouchControls, menuOpen, nativeImeActive) {
        if (!showTouchControls || menuOpen || nativeImeActive) resetOverlayAxes()
    }
    LaunchedEffect(menuOpen, gameId) {
        if (menuOpen && gameId.isNotBlank()) cheatSnapshot = CheatBridge.snapshot(gameId)
    }

    val saveStateRepository = remember(activity) { SaveStateRepository(activity) }
    var saveStateSlots by remember(activity, gameId) { mutableStateOf(emptyList<SaveStateSlot>()) }
    var selectedSaveStateSlot by remember(activity) { mutableIntStateOf(SaveStateRepository.QUICK_SLOT) }
    var saveStateBusy by remember { mutableStateOf(false) }
    var saveStateBusySaving by remember { mutableStateOf(false) }
    var pendingSaveStateAction by remember { mutableStateOf<SaveStateAction?>(null) }
    var saveStatesRefreshKey by remember { mutableIntStateOf(0) }

    LaunchedEffect(gameId, saveStatesRefreshKey) {
        if (gameId.isBlank()) {
            saveStateSlots = emptyList()
            return@LaunchedEffect
        }
        saveStateSlots = withContext(Dispatchers.IO) { saveStateRepository.listSlots(gameId) }
    }

    fun saveStateSlotName(slot: Int): String =
        if (slot == SaveStateRepository.QUICK_SLOT) {
            activity.getString(R.string.emulation_savestate_quick_label)
        } else {
            activity.getString(R.string.emulation_savestate_slot_label, slot)
        }

    val overlayScope = rememberCoroutineScope()

    fun performSaveStateAction(action: SaveStateAction) {
        if (saveStateBusy || gameId.isBlank()) return
        overlayScope.launch {
            saveStateBusy = true
            saveStateBusySaving = action is SaveStateAction.Save
            val result = try {
                withContext(Dispatchers.IO) {
                    when (action) {
                        is SaveStateAction.Save -> saveStateRepository.save(gameId, action.slot, BuildConfig.VERSION_NAME)
                        is SaveStateAction.Load -> saveStateRepository.load(gameId, action.slot)
                        is SaveStateAction.Delete -> saveStateRepository.delete(gameId, action.slot)
                    }
                }
            } catch (cancelled: CancellationException) {
                throw cancelled
            } catch (error: Exception) {
                Log.e("SaveStateOverlay", "Save-state action failed before returning a result", error)
                SaveStateResult(
                    status = SaveStateResult.STATUS_IO_ERROR,
                    error = error.message ?: "save state operation failed",
                    bytes = 0L,
                    titleId = gameId,
                    appVersion = "",
                    engineVersion = 0,
                    sessionMatch = true,
                    timestamp = 0L
                )
            } finally {
                saveStateBusy = false
                saveStateBusySaving = false
            }
            saveStatesRefreshKey++
            if (!result.isOk) {
                Log.e("SaveStateOverlay", "${action.javaClass.simpleName} failed (${result.status}): ${result.error}")
            }
            val messageRes = when {
                result.isOk -> when (action) {
                    is SaveStateAction.Save -> R.string.emulation_savestate_saved_toast
                    is SaveStateAction.Load -> R.string.emulation_savestate_loaded_toast
                    is SaveStateAction.Delete -> R.string.emulation_savestate_deleted_toast
                }
                result.isSessionMismatch -> R.string.emulation_savestate_session_mismatch_toast
                result.isTitleMismatch -> R.string.emulation_savestate_title_mismatch_toast
                else -> R.string.emulation_savestate_failed_toast
            }
            Toast.makeText(activity, messageRes, Toast.LENGTH_SHORT).show()
            if (result.isOk && action is SaveStateAction.Load) {
                menuOpen = false
                userPaused = false
            }
        }
    }

    DisposableEffect(activity, gameId) {
        activity.setCheatImportHandler { uri ->
            overlayScope.launch {
                val imported = withContext(Dispatchers.IO) {
                    runCatching {
                        val displayName = activity.contentResolver
                            .query(uri, arrayOf(OpenableColumns.DISPLAY_NAME), null, null, null)
                            ?.use { cursor -> if (cursor.moveToFirst()) cursor.getString(0) else null }
                        val temp = File(activity.cacheDir, "cheat_import.tmp")
                        activity.contentResolver.openInputStream(uri)?.use { input ->
                            temp.outputStream().use { output -> input.copyTo(output) }
                        }
                        CheatBridge.importFile(gameId, temp.absolutePath, displayName ?: "cheats.psv")
                    }.getOrDefault(VitaCheatSnapshot.EMPTY)
                }
                if (imported.cheats.isNotEmpty()) {
                    cheatSnapshot = imported
                    Toast.makeText(activity, R.string.emulation_cheats_import_success, Toast.LENGTH_SHORT).show()
                } else {
                    Toast.makeText(activity, R.string.emulation_cheats_import_failed, Toast.LENGTH_SHORT).show()
                }
            }
        }
        onDispose { activity.setCheatImportHandler(null) }
    }
    val gyroController = remember(activity, overlayBridge) {
        AndroidGyroscopeInput(activity) { emittedMode, x, y ->
            val rightStick = emittedMode == VitaCoreConfig.GYRO_MODE_AIM
            val axisX = if (rightStick) {
                InputOverlay.ControlId.axis_right_x
            } else {
                InputOverlay.ControlId.axis_left_x
            }
            val axisY = if (rightStick) {
                InputOverlay.ControlId.axis_right_y
            } else {
                InputOverlay.ControlId.axis_left_y
            }
            fun quantize(value: Float): Short = (value * Short.MAX_VALUE)
                .roundToInt()
                .coerceIn(Short.MIN_VALUE.toInt(), Short.MAX_VALUE.toInt())
                .toShort()
            overlayBridge.sendAxis(axisX, quantize(x))
            overlayBridge.sendAxis(axisY, quantize(y))
        }
    }

    fun persistConfig(transform: (VitaCoreConfig) -> VitaCoreConfig) {
        config = transform(config)
        // Only persist when we have a real game ID — if still blank the native
        // core hasn't reported the title yet and the save would be a no-op
        // (savePreservingDriverOverride guards against blank IDs internally).
        repository.savePreservingDriverOverride(gameId, config)
        activity.updateGamepadRuntimeInputSettings(config)
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
            config.frameLimit,
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

    DisposableEffect(
        lifecycleOwner,
        effectivePaused,
        nativeImeActive,
        config.gyroMode,
        config.gyroSensitivity,
        config.gyroSmoothing,
        config.gyroInvertX,
        config.gyroInvertY
    ) {
        fun startGyroscope() {
            if (!effectivePaused && !nativeImeActive && config.gyroMode != VitaCoreConfig.GYRO_MODE_OFF) {
                gyroController.start(
                    mode = config.gyroMode,
                    sensitivityPercent = config.gyroSensitivity,
                    smoothingPercent = config.gyroSmoothing,
                    invertX = config.gyroInvertX,
                    invertY = config.gyroInvertY
                )
            }
        }
        val observer = LifecycleEventObserver { _, event ->
            when (event) {
                Lifecycle.Event.ON_RESUME -> startGyroscope()
                Lifecycle.Event.ON_PAUSE,
                Lifecycle.Event.ON_STOP -> gyroController.stop()
                else -> Unit
            }
        }
        lifecycleOwner.lifecycle.addObserver(observer)
        if (lifecycleOwner.lifecycle.currentState.isAtLeast(Lifecycle.State.RESUMED)) {
            startGyroscope()
        }
        onDispose {
            lifecycleOwner.lifecycle.removeObserver(observer)
            gyroController.stop()
        }
    }

    LaunchedEffect(effectivePaused) {
        activity.setMenuPaused(effectivePaused)
    }

    LaunchedEffect(nativeImeActive) {
        if (nativeImeActive) {
            menuOpen = false
            controlsEditMode = false
            userPaused = false // Vita must keep running to consume text/Enter callbacks.
            overlayBridge.setIsInEditMode(false)
        }
    }

    LaunchedEffect(touchControlsActive) {
        overlayBridge.setTouchControlsActive(touchControlsActive)
    }

    LaunchedEffect(showTouchControls, inputResumed) {
        while (showTouchControls && inputResumed) {
            // The native session drops the virtual controller on an in-process relaunch
            // (LoadExec, e.g. the God of War Collection menu), so keep watching instead
            // of attaching once and assuming it stays attached.
            kotlinx.coroutines.delay(if (overlayBridge.ensureControllerAttached()) 1_000 else 350)
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
            activity.heartbeatPlayTimeSession()
            kotlinx.coroutines.delay(1_000)
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
        activity.setOverlayPauseMenuOpenHandler {
            if (!activity.launchStateLoading.active) {
                menuOpen = true
                menuButtonVisible = true
            }
        }
        onDispose {
            activity.setOverlayBackHandler(null)
            activity.setOverlayMenuButtonRevealHandler(null)
            activity.setOverlayPauseMenuOpenHandler(null)
            customizationPreferences.close()
        }
    }

    Box(modifier = modifier.fillMaxSize()) {
        if (showTouchControls && inputResumed) {
            OnScreenControls(
                modifier = Modifier.fillMaxSize(),
                overlayScale = config.overlayScale,
                overlayOpacity = config.overlayOpacity,
                showTouchSwitch = config.overlayShowTouchSwitch,
                touchMode = touchMode,
                visualStyle = customization.touchControlVisualStyle,
                pressEffect = customization.touchControlPressEffect,
                touchHaptics = config.touchHaptics,
                touchHapticsPreset = config.touchHapticsPreset,
                touchHapticsStrength = config.touchHapticsStrength,
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
                    touchMode = (touchMode + 1) % 3
                    overlayBridge.sendTouchState(touchMode)
                },
                onButtonChange = { button, pressed -> overlayBridge.sendButton(button, pressed) },
                onAxisChange = { axis, value -> overlayBridge.sendAxis(axis, value) }
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
                if (controlsVisible) {
                    // A gamepad caused the auto-hide, so only clear the session override
                    // and keep the user's persisted preference for when it disconnects.
                    if (hasPhysicalGamepad) {
                        showControlsWithGamepad = false
                    } else {
                        persistConfig { it.copy(enableGamepadOverlay = false) }
                    }
                } else {
                    showControlsWithGamepad = true
                    if (!config.enableGamepadOverlay) {
                        persistConfig { it.copy(enableGamepadOverlay = true) }
                    }
                }
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
                touchMode = 0
                overlayBridge.sendTouchState(0)
            },
            onTouchSwitch = { enabled ->
                persistConfig { it.copy(overlayShowTouchSwitch = enabled) }
                if (!enabled) {
                    touchMode = 0
                    overlayBridge.sendTouchState(0)
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
            onFrameLimit = { limit -> persistConfig { it.copy(frameLimit = limit) } },
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
            onGamepadVibrationStrength = { value -> persistConfig { it.copy(gamepadVibrationStrength = value) } },
            onDeviceVibrationFallback = { enabled -> persistConfig { it.copy(deviceVibrationFallback = enabled) } },
            onGamepadSwapSticks = { enabled -> persistConfig { it.copy(gamepadSwapSticks = enabled) } },
            onGamepadInvertLeftY = { enabled -> persistConfig { it.copy(gamepadInvertLeftY = enabled) } },
            onGamepadInvertRightY = { enabled -> persistConfig { it.copy(gamepadInvertRightY = enabled) } },
            onCheatsMaster = { enabled ->
                CheatBridge.setCheatsEnabled(enabled)
                coreConfigRepository.save(coreConfigRepository.load().copy(enableCheats = enabled))
                cheatSnapshot = CheatBridge.snapshot(gameId)
            },
            onCheatToggle = { index, enabled ->
                CheatBridge.setCheatEnabled(gameId, index, enabled)
                CheatBridge.saveCheats(gameId)
                cheatSnapshot = CheatBridge.snapshot(gameId)
            },
            onCheatsGroupToggle = { indices, enabled ->
                indices.forEach { index -> CheatBridge.setCheatEnabled(gameId, index, enabled) }
                CheatBridge.saveCheats(gameId)
                cheatSnapshot = CheatBridge.snapshot(gameId)
            },
            onCheatsImport = { activity.requestCheatImport() },
            onSaveStateSlotSelected = { slot -> selectedSaveStateSlot = slot },
            onSaveStateSave = { slot ->
                val exists = saveStateSlots.firstOrNull { it.slot == slot }?.exists == true
                if (exists) {
                    pendingSaveStateAction = SaveStateAction.Save(slot, overwrite = true)
                } else {
                    performSaveStateAction(SaveStateAction.Save(slot, overwrite = false))
                }
            },
            onSaveStateLoad = { slot ->
                val target = saveStateSlots.firstOrNull { it.slot == slot }
                if (target != null && target.exists) {
                    pendingSaveStateAction = SaveStateAction.Load(slot)
                }
            },
            onSaveStateDelete = { slot -> pendingSaveStateAction = SaveStateAction.Delete(slot) },
            onQuickSaveState = {
                performSaveStateAction(SaveStateAction.Save(SaveStateRepository.QUICK_SLOT, overwrite = false))
            },
            onQuickLoadState = {
                val quick = saveStateSlots.firstOrNull { it.slot == SaveStateRepository.QUICK_SLOT }
                if (quick == null || !quick.exists) {
                    Toast.makeText(activity, R.string.emulation_savestate_empty, Toast.LENGTH_SHORT).show()
                } else {
                    performSaveStateAction(SaveStateAction.Load(SaveStateRepository.QUICK_SLOT))
                }
            }
        )

        AnimatedVisibility(
            visible = !nativeImeActive && !controlsEditMode && menuButtonVisible,
            enter = fadeIn(tween(180)),
            exit = fadeOut(tween(140)),
            modifier = Modifier.align(Alignment.TopCenter)
        ) {
            EmulationQuickBar(
                paused = effectivePaused,
                quickActionsEnabled = !saveStateBusy && gameId.isNotBlank(),
                onPauseToggle = menuCallbacks.onPauseToggle,
                onQuickSave = menuCallbacks.onQuickSaveState,
                onQuickLoad = menuCallbacks.onQuickLoadState,
                onScreenshot = { activity.requestScreenshot() },
                onOpenMenu = {
                    menuOpen = !menuOpen
                    menuButtonVisible = true
                }
            )
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
            modifier = Modifier
                .align(if (useSidePanel) Alignment.CenterEnd else Alignment.BottomCenter)
                .padding(
                    start = if (useSidePanel) 0.dp else 16.dp,
                    top = 16.dp,
                    end = 16.dp,
                    bottom = 16.dp
                )
        ) {
            EmulationGameMenu(
                gameTitle = gameTitle,
                gameId = gameId,
                gameIconPath = gameIconPath,
                config = config,
                cheats = cheatSnapshot,
                cheatsAvailable = CHEATS_ENABLED || customization.experimentalCheats,
                saveStates = SaveStateMenuState(
                    slots = saveStateSlots,
                    selectedSlot = selectedSaveStateSlot,
                    busy = saveStateBusy,
                    busySaving = saveStateBusySaving
                ),
                paused = effectivePaused,
                sessionElapsedMs = sessionElapsedMs,
                expandHorizontally = useSidePanel,
                layoutStyle = customization.gameMenuLayoutStyle,
                physicalGamepadConnected = hasPhysicalGamepad,
                controlsVisible = controlsVisible,
                callbacks = menuCallbacks
            )
        }

        NativeImeOverlay(activity)

        pendingSaveStateAction?.let { action ->
            val titleRes = when (action) {
                is SaveStateAction.Save -> R.string.emulation_savestate_confirm_save_title
                is SaveStateAction.Load -> R.string.emulation_savestate_confirm_load_title
                is SaveStateAction.Delete -> R.string.emulation_savestate_confirm_delete_title
            }
            val bodyRes = when (action) {
                is SaveStateAction.Save -> R.string.emulation_savestate_confirm_save_body
                is SaveStateAction.Load -> R.string.emulation_savestate_confirm_load_body
                is SaveStateAction.Delete -> R.string.emulation_savestate_confirm_delete_body
            }
            val confirmRes = when (action) {
                is SaveStateAction.Save -> R.string.emulation_savestate_save_action
                is SaveStateAction.Load -> R.string.emulation_savestate_load_action
                is SaveStateAction.Delete -> R.string.emulation_savestate_delete_action
            }
            AlertDialog(
                onDismissRequest = { pendingSaveStateAction = null },
                title = { Text(text = stringResource(titleRes)) },
                text = { Text(text = stringResource(bodyRes, saveStateSlotName(action.slot))) },
                confirmButton = {
                    TextButton(
                        onClick = {
                            pendingSaveStateAction = null
                            performSaveStateAction(action)
                        }
                    ) {
                        Text(text = stringResource(confirmRes))
                    }
                },
                dismissButton = {
                    TextButton(onClick = { pendingSaveStateAction = null }) {
                        Text(text = stringResource(R.string.common_cancel))
                    }
                }
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

        LaunchStateLoadingOverlay(launchStateLoading)
    }

    LaunchedEffect(hasPhysicalGamepad) {
        if (!hasPhysicalGamepad) {
            // Reconnects fall back to the persisted preference instead of the override.
            showControlsWithGamepad = false
        }
        if (hasPhysicalGamepad && touchMode != 0) {
            touchMode = 0
            overlayBridge.sendTouchState(0)
        }
    }
}

@SuppressLint("ConfigurationScreenWidthHeight")
@Composable
internal fun OnScreenControls(
    overlayScale: Float,
    overlayOpacity: Int,
    showTouchSwitch: Boolean,
    touchMode: Int,
    visualStyle: TouchControlVisualStyle,
    pressEffect: TouchControlPressEffect,
    touchHaptics: Boolean,
    touchHapticsPreset: Int,
    touchHapticsStrength: Int,
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
    val context = LocalContext.current
    val hapticView = LocalView.current
    val configuration = LocalConfiguration.current
    val isLandscape = configuration.screenWidthDp > configuration.screenHeightDp
    val cutoutInsets = WindowInsets.displayCutout.asPaddingValues()
    val navInsets = WindowInsets.navigationBars.asPaddingValues()
    val topInset = maxOf(
        cutoutInsets.calculateTopPadding(),
        WindowInsets.statusBarsIgnoringVisibility.asPaddingValues().calculateTopPadding(),
    )
    val bottomInset = navInsets.calculateBottomPadding()
    val sideInset = maxOf(
        cutoutInsets.calculateLeftPadding(androidx.compose.ui.unit.LayoutDirection.Ltr),
        cutoutInsets.calculateRightPadding(androidx.compose.ui.unit.LayoutDirection.Ltr)
    )

    val alpha = overlayOpacity / 100f
    val sidePadding = sideInset + if (isLandscape) 16.dp else 10.dp
    val bottomPadding = bottomInset + if (isLandscape) 22.dp else 16.dp
    val shoulderTopPadding = topInset + if (isLandscape) 22.dp else 16.dp

    BoxWithConstraints(modifier = modifier.fillMaxSize()) {
        if (editMode) {
            Box(Modifier.fillMaxSize().background(Color.Black.copy(alpha = 0.34f)))
        }
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
        var selectedGroupIds by remember(editMode) { mutableStateOf<Set<String>?>(null) }
        var groupScalePercent by remember(editMode) { mutableIntStateOf(100) }
        var pressedGroupControlIds by remember { mutableStateOf(emptySet<Int>()) }
        var hapticPressedControlIds by remember { mutableStateOf(emptySet<Int>()) }
        LaunchedEffect(mergedLayout) {
            controls = mergedLayout
        }
        val selected = controls.firstOrNull { it.id == selectedId } ?: controls.firstOrNull()
        var showGrid by rememberSaveable { mutableStateOf(false) }
        var snapToGrid by rememberSaveable { mutableStateOf(false) }
        var comboEditorOpen by remember { mutableStateOf(false) }
        var createCombo by remember { mutableStateOf(false) }
        val dragResiduals = remember { mutableMapOf<String, Pair<Float, Float>>() }
        val gridStep = with(density) { 24.dp.toPx() }
        val selectedDescriptor = selected?.actionId?.let(::touchControlDescriptor)
        val defaultSelected = selected?.let { element -> defaultLayout.firstOrNull { it.id == element.actionId } }
        val selectedIsAnalog = selectedDescriptor?.type == TouchControlType.Analog
        val selectedAnalogMode = selected?.analogMode ?: TouchAnalogMode.Stick
        val selectedScalePercent = if (selected != null && defaultSelected != null) {
            val currentSize = maxOf(selected.width * canvasWidth, selected.height * canvasHeight)
            val defaultSize = maxOf(defaultSelected.width * canvasWidth, defaultSelected.height * canvasHeight).coerceAtLeast(1f)
            ((currentSize / defaultSize) * 100f).roundToInt().coerceIn(25, 300)
        } else {
            100
        }
        val selectedWidthPercent = if (selected != null && defaultSelected != null) {
            ((selected.width / defaultSelected.width.coerceAtLeast(0.001f)) * 100f).roundToInt().coerceIn(25, 300)
        } else {
            100
        }
        val selectedHeightPercent = if (selected != null && defaultSelected != null) {
            ((selected.height / defaultSelected.height.coerceAtLeast(0.001f)) * 100f).roundToInt().coerceIn(25, 300)
        } else {
            100
        }

        fun commitLayoutChange(transform: (List<TouchControlElement>) -> List<TouchControlElement>) {
            val updated = transform(controls).map { it.coerceToCanvas() }
            controls = updated
            onLayoutChange(updated)
        }

        fun snapDrag(id: String, x: Float, y: Float, delta: Offset): Offset {
            val snapped = snapTouchDrag(dragResiduals, id, x * canvasWidth, y * canvasHeight,
                delta.x to delta.y, gridStep, snapToGrid)
            return Offset(snapped.first, snapped.second)
        }

        fun duplicateSelected() {
            val source = selected ?: return
            if (selectedDescriptor?.type != TouchControlType.Button || controls.count { it.id.startsWith("custom_") } >= 32) return
            val copy = source.copy(id = "custom_" + java.util.UUID.randomUUID(),
                x = source.x + 0.04f, y = source.y + 0.04f).coerceToCanvas()
            commitLayoutChange { it + copy }
            selectedId = copy.id
        }

        fun resizeAroundCenter(element: TouchControlElement, nextWidth: Float, nextHeight: Float): TouchControlElement {
            val centerX = element.x + element.width / 2f
            val centerY = element.y + element.height / 2f
            val safeWidth = nextWidth.coerceIn(0.015f, 0.5f)
            val safeHeight = nextHeight.coerceIn(0.015f, 0.5f)
            return element.copy(
                width = safeWidth,
                height = safeHeight,
                x = (centerX - safeWidth / 2f).coerceIn(0f, 1f - safeWidth),
                y = (centerY - safeHeight / 2f).coerceIn(0f, 1f - safeHeight)
            )
        }

        fun updateSelectedSize(percentDelta: Int) {
            val selectedElement = selected ?: return
            val target = controls.firstOrNull { it.id == selectedElement.id } ?: selectedElement
            val baseline = defaultLayout.firstOrNull { it.id == target.actionId } ?: target
            val currentSize = maxOf(target.width * canvasWidth, target.height * canvasHeight)
            val defaultSize = maxOf(baseline.width * canvasWidth, baseline.height * canvasHeight).coerceAtLeast(1f)
            val currentPercent = ((currentSize / defaultSize) * 100f).roundToInt().coerceIn(25, 300)
            val nextPercent = (currentPercent + percentDelta).coerceIn(35, 250) / 100f
            val nextWidth = (baseline.width * nextPercent).coerceIn(0.015f, 0.5f)
            val nextHeight = (baseline.height * nextPercent).coerceIn(0.015f, 0.5f)
            commitLayoutChange { currentControls ->
                currentControls.replaceElement(resizeAroundCenter(target, nextWidth, nextHeight))
            }
        }

        fun updateSelectedWidth(percentDelta: Int) {
            val selectedElement = selected ?: return
            val target = controls.firstOrNull { it.id == selectedElement.id } ?: selectedElement
            val baseline = defaultLayout.firstOrNull { it.id == target.actionId } ?: target
            val currentPercent = ((target.width / baseline.width.coerceAtLeast(0.001f)) * 100f).roundToInt().coerceIn(25, 300)
            val nextPercent = (currentPercent + percentDelta).coerceIn(50, 300) / 100f
            commitLayoutChange { currentControls ->
                currentControls.replaceElement(resizeAroundCenter(target, baseline.width * nextPercent, target.height))
            }
        }

        fun updateSelectedHeight(percentDelta: Int) {
            val selectedElement = selected ?: return
            val target = controls.firstOrNull { it.id == selectedElement.id } ?: selectedElement
            val baseline = defaultLayout.firstOrNull { it.id == target.actionId } ?: target
            val currentPercent = ((target.height / baseline.height.coerceAtLeast(0.001f)) * 100f).roundToInt().coerceIn(25, 300)
            val nextPercent = (currentPercent + percentDelta).coerceIn(50, 300) / 100f
            commitLayoutChange { currentControls ->
                currentControls.replaceElement(resizeAroundCenter(target, target.width, baseline.height * nextPercent))
            }
        }

        fun updateSelectedOpacity(percentDelta: Int) {
            val selectedElement = selected ?: return
            val target = controls.firstOrNull { it.id == selectedElement.id } ?: selectedElement
            val nextOpacity = (target.opacity + percentDelta)
                .coerceIn(CONTROL_OPACITY_MIN, CONTROL_OPACITY_MAX)
            if (nextOpacity == target.opacity) return
            commitLayoutChange { currentControls ->
                currentControls.replaceElement(target.copy(opacity = nextOpacity))
            }
        }

        fun selectElementControl(id: String) {
            selectedId = id
            selectedGroupIds = null
            groupScalePercent = 100
            dragResiduals.clear()
        }

        fun selectControlGroup(ids: Set<String>) {
            if (selectedGroupIds != ids) {
                groupScalePercent = 100
            }
            selectedId = null
            selectedGroupIds = ids
            dragResiduals.clear()
        }

        fun updateGroupScale(percentDelta: Int) {
            val groupIds = selectedGroupIds ?: return
            val nextPercent = (groupScalePercent + percentDelta)
                .coerceIn(GROUP_SCALE_MIN_PERCENT, GROUP_SCALE_MAX_PERCENT)
            if (nextPercent == groupScalePercent) return
            val members = controls.filter { it.id in groupIds }
            if (members.size != groupIds.size) return
            val factor = nextPercent.toFloat() / groupScalePercent.toFloat()
            commitLayoutChange { currentControls ->
                currentControls.replaceElements(members.scaleGroupAroundCenter(factor))
            }
            groupScalePercent = nextPercent
        }

        fun resetSelectedElementOrGroup() {
            val groupIds = selectedGroupIds
            if (groupIds != null) {
                val defaults = defaultLayout.filter { it.id in groupIds }
                if (defaults.size == groupIds.size) {
                    commitLayoutChange { currentControls -> currentControls.replaceElements(defaults) }
                }
                groupScalePercent = 100
                return
            }
            val currentSelected = selected ?: return
            defaultSelected?.let { baseline ->
                val reset = if (currentSelected.id.startsWith("custom_")) {
                    baseline.copy(id = currentSelected.id, x = 0.45f, y = 0.45f,
                        actionId = currentSelected.actionId, secondaryActionId = currentSelected.secondaryActionId,
                        visible = currentSelected.visible)
                } else baseline
                commitLayoutChange { it.replaceElement(reset) }
            }
        }

        fun toggleSelectedAnalogMode() {
            val selectedElement = selected ?: return
            val target = controls.firstOrNull { it.id == selectedElement.id } ?: selectedElement
            val baseline = defaultLayout.firstOrNull { it.id == target.actionId } ?: target
            val nextMode = if (target.analogMode == TouchAnalogMode.TouchArea) {
                TouchAnalogMode.Stick
            } else {
                TouchAnalogMode.TouchArea
            }
            val resized = if (nextMode == TouchAnalogMode.TouchArea) {
                resizeAroundCenter(
                    element = target,
                    nextWidth = maxOf(target.width, baseline.width * 1.8f),
                    nextHeight = maxOf(target.height, baseline.height * 1.1f)
                )
            } else {
                resizeAroundCenter(target, baseline.width, baseline.height)
            }
            commitLayoutChange { currentControls ->
                currentControls.replaceElement(resized.copy(analogMode = nextMode))
            }
        }

        if (editMode && showGrid) {
            androidx.compose.foundation.Canvas(Modifier.fillMaxSize()) {
                var x = 0f
                while (x <= size.width) {
                    drawLine(Color.White.copy(alpha = 0.16f), Offset(x, 0f), Offset(x, size.height))
                    x += gridStep
                }
                var y = 0f
                while (y <= size.height) {
                    drawLine(Color.White.copy(alpha = 0.16f), Offset(0f, y), Offset(size.width, y))
                    y += gridStep
                }
            }
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
                        selected = selectedGroupIds == group.ids,
                        onSelected = { selectControlGroup(group.ids) },
                        onDragStart = { selectControlGroup(group.ids) },
                        snapDrag = ::snapDrag,
                        onGroupChange = { updatedElements ->
                            commitLayoutChange { currentControls ->
                                currentControls.replaceElements(updatedElements)
                            }
                        }
                    )
                }
            }
        }

        val activeInputGroups = touchControlGroups.filter { group ->
            group.ids.all { id -> controls.any { it.id == id && it.visible } }
        }
        val groupHandledControlIds = activeInputGroups.flatMap { it.ids }.toSet()
        fun performTouchHaptic(phase: ButtonPhase) {
            if (touchHaptics) {
                AndroidTouchHaptics.playButton(
                    context = context,
                    view = hapticView,
                    strengthPercent = touchHapticsStrength,
                    preset = touchHapticsPreset,
                    phase = phase
                )
            }
        }

        fun dispatchButtonChange(controlId: Int, pressed: Boolean) {
            // Send the press to the core first. Haptics and Compose state are
            // presentation concerns; doing them before this adds their cost
            // straight onto input latency.
            onButtonChange(controlId, pressed)

            val wasPressed = controlId in hapticPressedControlIds
            if (pressed != wasPressed) {
                hapticPressedControlIds = if (pressed) {
                    hapticPressedControlIds + controlId
                } else {
                    hapticPressedControlIds - controlId
                }
                performTouchHaptic(if (pressed) ButtonPhase.PRESS else ButtonPhase.RELEASE)
            }
        }

        val currentDispatch by rememberUpdatedState<(Int, Boolean) -> Unit>(::dispatchButtonChange)
        val actionTracker = remember { TouchActionTracker { action, pressed -> currentDispatch(action, pressed) } }
        DisposableEffect(actionTracker, editMode) {
            onDispose { actionTracker.cancel() }
        }
        fun dispatchControlChange(id: String, pressed: Boolean) {
            if (!pressed) {
                actionTracker.release(id)
                return
            }
            val element = controls.firstOrNull { it.id == id } ?: return
            val actions = listOfNotNull(element.actionId, element.secondaryActionId)
                .mapNotNull { touchControlDescriptor(it)?.controlId }.toSet()
            actionTracker.press(id, actions)
        }

        fun handleGroupButtonChange(id: String, pressed: Boolean) {
            dispatchControlChange(id, pressed)
            val controlId = controls.firstOrNull { it.id == id }?.actionId?.let(::touchControlDescriptor)?.controlId ?: return
            pressedGroupControlIds = if (pressed) {
                pressedGroupControlIds + controlId
            } else {
                pressedGroupControlIds - controlId
            }
        }

        if (!editMode) {
            activeInputGroups.forEach { group ->
                val groupElements = controls.filter { it.id in group.ids && it.visible }
                if (groupElements.size == group.ids.size) {
                    TouchControlGroupInputCapture(
                        groupElements = groupElements,
                        canvasWidth = canvasWidth,
                        canvasHeight = canvasHeight,
                        onButtonChange = ::handleGroupButtonChange
                    )
                }
            }
        }

        controls.forEach { element ->
            val descriptor = touchControlDescriptor(element.actionId) ?: return@forEach
            if (!editMode && (!element.visible || (element.id == TouchControlIds.TOUCH && !showTouchSwitch))) {
                return@forEach
            }
            key(element.id) {
              TouchControlCanvasItem(
                element = element,
                descriptor = descriptor,
                canvasWidth = canvasWidth,
                canvasHeight = canvasHeight,
                alpha = if (editMode && !element.visible) 0.28f else alpha * (element.opacity / 100f),
                selected = editMode && selectedGroupIds == null && selected?.id == element.id,
                editMode = editMode,
                inputHandledByGroup = !editMode && element.id in groupHandledControlIds,
                externallyPressed = descriptor.controlId?.let { it in pressedGroupControlIds } == true,
                touchMode = touchMode,
                visualStyle = visualStyle,
                pressEffect = pressEffect,
                onSelected = { selectElementControl(element.id) },
                snapDrag = ::snapDrag,
                onElementChange = { updated -> commitLayoutChange { currentControls -> currentControls.replaceElement(updated) } },
                onBackTouchToggle = onBackTouchToggle,
                onButtonChange = { _, pressed -> dispatchControlChange(element.id, pressed) },
                onAxisChange = onAxisChange
              )
            }
        }

        if (editMode && selected != null && selectedDescriptor != null) {
            val groupActive = selectedGroupIds != null
            TouchControlEditorChrome(
                selectedLabel = if (groupActive) {
                    if (TouchControlIds.DPAD_UP in selectedGroupIds.orEmpty()) {
                        stringResource(R.string.controls_editor_group_dpad)
                    } else {
                        stringResource(R.string.controls_editor_group_buttons)
                    }
                } else {
                    listOfNotNull(
                        stringResource(selectedDescriptor.labelRes),
                        selected.secondaryActionId?.let(::touchControlDescriptor)?.let { stringResource(it.labelRes) }
                    ).joinToString(" + ")
                },
                selectedVisible = selected.visible,
                selectedScalePercent = selectedScalePercent,
                onReset = onEditReset,
                onVisibilityToggle = {
                    val currentSelected = controls.firstOrNull { it.id == selected.id } ?: selected
                    commitLayoutChange { currentControls ->
                        currentControls.replaceElement(currentSelected.copy(visible = !currentSelected.visible))
                    }
                },
                onSizeDecrease = { updateSelectedSize(-10) },
                onSizeIncrease = { updateSelectedSize(10) },
                selectedOpacityPercent = selected.opacity,
                onOpacityDecrease = { updateSelectedOpacity(-CONTROL_OPACITY_STEP) },
                onOpacityIncrease = { updateSelectedOpacity(CONTROL_OPACITY_STEP) },
                analogMode = if (!groupActive && selectedIsAnalog) selectedAnalogMode else null,
                touchAreaWidthPercent = selectedWidthPercent,
                touchAreaHeightPercent = selectedHeightPercent,
                onAnalogModeToggle = ::toggleSelectedAnalogMode,
                onTouchAreaWidthDecrease = { updateSelectedWidth(-10) },
                onTouchAreaWidthIncrease = { updateSelectedWidth(10) },
                onTouchAreaHeightDecrease = { updateSelectedHeight(-10) },
                onTouchAreaHeightIncrease = { updateSelectedHeight(10) },
                onDone = onEditDone,
                showDimensions = !groupActive && (!selectedIsAnalog || selectedAnalogMode == TouchAnalogMode.TouchArea),
                canDuplicate = !groupActive && selectedDescriptor.type == TouchControlType.Button && controls.count { it.id.startsWith("custom_") } < 32,
                canDelete = !groupActive && selected.id.startsWith("custom_"),
                canCombo = !groupActive && selectedDescriptor.type == TouchControlType.Button,
                canCreate = controls.count { it.id.startsWith("custom_") } < 32,
                showGrid = showGrid,
                snapToGrid = snapToGrid,
                onDuplicate = ::duplicateSelected,
                onDelete = {
                    commitLayoutChange { it.filterNot { element -> element.id == selected.id } }
                    selectedId = null
                    selectedGroupIds = null
                },
                onCombo = { createCombo = false; comboEditorOpen = true },
                onCreate = { createCombo = true; comboEditorOpen = true },
                onResetSelected = { resetSelectedElementOrGroup() },
                onGridToggle = { showGrid = !showGrid },
                onSnapToggle = { snapToGrid = !snapToGrid; dragResiduals.clear() },
                groupSelected = groupActive,
                groupScalePercent = groupScalePercent,
                onGroupScaleDecrease = { updateGroupScale(-GROUP_SCALE_STEP_PERCENT) },
                onGroupScaleIncrease = { updateGroupScale(GROUP_SCALE_STEP_PERCENT) },
                modifier = Modifier
                    .align(Alignment.TopCenter)
                    .heightIn(max = maxHeight * 0.62f)
            )
        }

        if (editMode && comboEditorOpen) {
            TouchComboEditorDialog(
                actions = listOf(TouchControlIds.DPAD_UP, TouchControlIds.DPAD_DOWN, TouchControlIds.DPAD_LEFT,
                    TouchControlIds.DPAD_RIGHT, TouchControlIds.TRIANGLE, TouchControlIds.CROSS, TouchControlIds.SQUARE,
                    TouchControlIds.CIRCLE, TouchControlIds.L1, TouchControlIds.R1, TouchControlIds.L2, TouchControlIds.R2,
                    TouchControlIds.SELECT, TouchControlIds.START).mapNotNull { id -> touchControlDescriptor(id)?.let { id to stringResource(it.labelRes) } },
                primary = if (createCombo) TouchControlIds.CROSS else selected?.actionId ?: TouchControlIds.CROSS,
                secondary = if (createCombo) TouchControlIds.L1 else selected?.secondaryActionId,
                primaryEditable = createCombo || selected?.id?.startsWith("custom_") == true,
                onDismiss = { comboEditorOpen = false },
                onConfirm = { primary, secondary ->
                    if (createCombo) {
                        val baseline = defaultLayout.first { it.id == primary }
                        val created = baseline.copy(id = "custom_" + java.util.UUID.randomUUID(),
                            x = 0.45f, y = 0.45f, visible = true, secondaryActionId = secondary)
                        commitLayoutChange { it + created }
                        selectedId = created.id
                    } else if (selected != null) {
                        commitLayoutChange { it.replaceElement(selected.copy(actionId = primary, secondaryActionId = secondary)) }
                    }
                    comboEditorOpen = false
                }
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
    val labelRes: Int,
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

private val TouchGroupDragCapturePadding = 28.dp

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
    TouchControlIds.L2 -> TouchControlDescriptor(id, R.string.controls_button_l2, R.drawable.ic_controller_l2_button, RoundedCornerShape(10.dp), TouchControlType.Button, InputOverlay.ControlId.l2)
    TouchControlIds.L1 -> TouchControlDescriptor(id, R.string.controls_button_l1, R.drawable.ic_controller_l1_button, RoundedCornerShape(10.dp), TouchControlType.Button, InputOverlay.ControlId.l1)
    TouchControlIds.R2 -> TouchControlDescriptor(id, R.string.controls_button_r2, R.drawable.ic_controller_r2_button, RoundedCornerShape(10.dp), TouchControlType.Button, InputOverlay.ControlId.r2)
    TouchControlIds.R1 -> TouchControlDescriptor(id, R.string.controls_button_r1, R.drawable.ic_controller_r1_button, RoundedCornerShape(10.dp), TouchControlType.Button, InputOverlay.ControlId.r1)
    TouchControlIds.DPAD_UP -> TouchControlDescriptor(id, R.string.controls_button_up, R.drawable.ic_controller_up_button, RoundedCornerShape(8.dp), TouchControlType.Button, InputOverlay.ControlId.dup)
    TouchControlIds.DPAD_DOWN -> TouchControlDescriptor(id, R.string.controls_button_down, R.drawable.ic_controller_down_button, RoundedCornerShape(8.dp), TouchControlType.Button, InputOverlay.ControlId.ddown)
    TouchControlIds.DPAD_LEFT -> TouchControlDescriptor(id, R.string.controls_button_left, R.drawable.ic_controller_left_button, RoundedCornerShape(8.dp), TouchControlType.Button, InputOverlay.ControlId.dleft)
    TouchControlIds.DPAD_RIGHT -> TouchControlDescriptor(id, R.string.controls_button_right, R.drawable.ic_controller_right_button, RoundedCornerShape(8.dp), TouchControlType.Button, InputOverlay.ControlId.dright)
    TouchControlIds.LEFT_STICK -> TouchControlDescriptor(id, R.string.controls_button_left_stick, R.drawable.ic_controller_analog_base, CircleShape, TouchControlType.Analog, axisX = InputOverlay.ControlId.axis_left_x, axisY = InputOverlay.ControlId.axis_left_y)
    TouchControlIds.RIGHT_STICK -> TouchControlDescriptor(id, R.string.controls_button_right_stick, R.drawable.ic_controller_analog_base, CircleShape, TouchControlType.Analog, axisX = InputOverlay.ControlId.axis_right_x, axisY = InputOverlay.ControlId.axis_right_y)
    TouchControlIds.TRIANGLE -> TouchControlDescriptor(id, R.string.controls_button_triangle, R.drawable.ic_controller_triangle_button, CircleShape, TouchControlType.Button, InputOverlay.ControlId.y)
    TouchControlIds.CROSS -> TouchControlDescriptor(id, R.string.controls_button_cross, R.drawable.ic_controller_cross_button, CircleShape, TouchControlType.Button, InputOverlay.ControlId.a)
    TouchControlIds.SQUARE -> TouchControlDescriptor(id, R.string.controls_button_square, R.drawable.ic_controller_square_button, CircleShape, TouchControlType.Button, InputOverlay.ControlId.x)
    TouchControlIds.CIRCLE -> TouchControlDescriptor(id, R.string.controls_button_circle, R.drawable.ic_controller_circle_button, CircleShape, TouchControlType.Button, InputOverlay.ControlId.b)
    TouchControlIds.SELECT -> TouchControlDescriptor(id, R.string.controls_button_select, R.drawable.ic_controller_select_button, RoundedCornerShape(8.dp), TouchControlType.Button, InputOverlay.ControlId.select)
    TouchControlIds.START -> TouchControlDescriptor(id, R.string.controls_button_start, R.drawable.ic_controller_start_button, RoundedCornerShape(8.dp), TouchControlType.Button, InputOverlay.ControlId.start)
    TouchControlIds.TOUCH -> TouchControlDescriptor(id, R.string.controls_button_touch, R.drawable.button_touch_f, RoundedCornerShape(8.dp), TouchControlType.TouchSwitch)
    else -> null
}

@Composable
private fun TouchControlGroupFrame(
    group: TouchControlGroup,
    elements: List<TouchControlElement>,
    canvasWidth: Float,
    canvasHeight: Float,
    selected: Boolean,
    onSelected: () -> Unit,
    onDragStart: () -> Unit,
    snapDrag: (String, Float, Float, Offset) -> Offset,
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
            .background(MaterialTheme.colorScheme.primary.copy(alpha = if (selected) 0.16f else 0.08f))
            .border(
                width = if (selected) 2.dp else 1.dp,
                color = MaterialTheme.colorScheme.primary.copy(alpha = if (selected) 0.75f else 0.42f),
                shape = RoundedCornerShape(18.dp)
            )
            .pointerInput(group.ids, selected) {
                detectTapGestures { onSelected() }
            }
            .pointerInput(group.ids, canvasWidth, canvasHeight) {
                var draggedElements = latestElements
                detectDragGestures(
                    onDragStart = {
                        draggedElements = latestElements
                        onDragStart()
                    }
                ) { change, dragAmount ->
                    change.consume()
                    val bounds = draggedElements.groupBounds()
                    val delta = snapDrag("group:" + group.ids.joinToString(), bounds.x, bounds.y, dragAmount)
                    draggedElements = draggedElements.moveGroupBy(
                        dx = delta.x / canvasWidth,
                        dy = delta.y / canvasHeight
                    )
                    onGroupChange(draggedElements)
                }
            }
    )
}

@Composable
private fun TouchControlGroupInputCapture(
    groupElements: List<TouchControlElement>,
    canvasWidth: Float,
    canvasHeight: Float,
    onButtonChange: (String, Boolean) -> Unit
) {
    val density = LocalDensity.current
    val currentOnButtonChange by rememberUpdatedState(onButtonChange)
    val buttonTracker = remember(groupElements, canvasWidth, canvasHeight) { TouchButtonTracker() }
    val bounds = groupElements.groupBounds()
    val paddingPx = with(density) { TouchGroupDragCapturePadding.toPx() }
    val paddedX = (bounds.x * canvasWidth - paddingPx).coerceAtLeast(0f)
    val paddedY = (bounds.y * canvasHeight - paddingPx).coerceAtLeast(0f)
    val paddedRight = ((bounds.x + bounds.width) * canvasWidth + paddingPx).coerceAtMost(canvasWidth)
    val paddedBottom = ((bounds.y + bounds.height) * canvasHeight + paddingPx).coerceAtMost(canvasHeight)
    val widthPx = (paddedRight - paddedX).coerceAtLeast(1f)
    val heightPx = (paddedBottom - paddedY).coerceAtLeast(1f)

    fun releasePointer(pointerId: Int) {
        buttonTracker.release(pointerId) { index, pressed -> currentOnButtonChange(groupElements[index].id, pressed) }
    }

    fun releaseAll() {
        buttonTracker.cancel { index, pressed -> currentOnButtonChange(groupElements[index].id, pressed) }
    }

    fun controlAt(localX: Float, localY: Float): Int? {
        val absoluteX = paddedX + localX
        val absoluteY = paddedY + localY
        return groupElements.indices.firstOrNull { index ->
            val element = groupElements[index]
            val left = element.x * canvasWidth
            val top = element.y * canvasHeight
            val right = left + element.width * canvasWidth
            val bottom = top + element.height * canvasHeight
            absoluteX in left..right && absoluteY in top..bottom
        }
    }

    fun updatePointer(event: MotionEvent, pointerIndex: Int) {
        val pointerId = event.getPointerId(pointerIndex)
        val nextControl = controlAt(event.getX(pointerIndex), event.getY(pointerIndex))
        buttonTracker.update(pointerId, nextControl) { index, pressed -> currentOnButtonChange(groupElements[index].id, pressed) }
    }

    DisposableEffect(buttonTracker) {
        onDispose { releaseAll() }
    }

    Box(
        modifier = Modifier
            .offset { IntOffset(paddedX.roundToInt(), paddedY.roundToInt()) }
            .size(
                width = with(density) { widthPx.toDp() },
                height = with(density) { heightPx.toDp() }
            )
            .pointerInteropFilter { event ->
                when (event.actionMasked) {
                    MotionEvent.ACTION_DOWN, MotionEvent.ACTION_POINTER_DOWN -> {
                        updatePointer(event, event.actionIndex)
                        true
                    }

                    MotionEvent.ACTION_MOVE -> {
                        for (index in 0 until event.pointerCount) {
                            updatePointer(event, index)
                        }
                        true
                    }

                    MotionEvent.ACTION_UP, MotionEvent.ACTION_POINTER_UP -> {
                        releasePointer(event.getPointerId(event.actionIndex))
                        true
                    }

                    MotionEvent.ACTION_CANCEL -> {
                        releaseAll()
                        true
                    }

                    else -> true
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
    inputHandledByGroup: Boolean,
    externallyPressed: Boolean,
    touchMode: Int,
    visualStyle: TouchControlVisualStyle,
    pressEffect: TouchControlPressEffect,
    onSelected: () -> Unit,
    snapDrag: (String, Float, Float, Offset) -> Offset,
    onElementChange: (TouchControlElement) -> Unit,
    onBackTouchToggle: () -> Unit,
    onButtonChange: (Int, Boolean) -> Unit,
    onAxisChange: (Int, Short) -> Unit
) {
    val density = LocalDensity.current
    val latestElement by rememberUpdatedState(element)
    val currentOnButtonChange by rememberUpdatedState(onButtonChange)
    val xPx = element.x * canvasWidth
    val yPx = element.y * canvasHeight
    val widthPx = element.width * canvasWidth
    val heightPx = element.height * canvasHeight
    var pressed by remember(element.id, editMode) { mutableStateOf(false) }
    val pointerOwner = remember(element.id, editMode) { TouchPointerOwner() }
    val itemShape = if (descriptor.type == TouchControlType.Analog && element.analogMode == TouchAnalogMode.TouchArea) {
        RoundedCornerShape(18.dp)
    } else {
        descriptor.shape
    }
    val sizeModifier = Modifier
        .offset { IntOffset(xPx.roundToInt(), yPx.roundToInt()) }
        .size(width = with(density) { widthPx.toDp() }, height = with(density) { heightPx.toDp() })

    DisposableEffect(element.id, editMode, inputHandledByGroup, descriptor.type, descriptor.controlId) {
        onDispose {
            if (pointerOwner.cancel() && descriptor.type == TouchControlType.Button) {
                descriptor.controlId?.let { currentOnButtonChange(it, false) }
            }
        }
    }

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
                    val delta = snapDrag(element.id, draggedElement.x, draggedElement.y, dragAmount)
                    draggedElement = draggedElement.copy(
                        x = (draggedElement.x + delta.x / canvasWidth).coerceIn(0f, 1f - draggedElement.width),
                        y = (draggedElement.y + delta.y / canvasHeight).coerceIn(0f, 1f - draggedElement.height)
                    )
                    onElementChange(draggedElement)
                }
            }
            .border(
                width = if (selected) 2.dp else 1.dp,
                color = if (selected) MaterialTheme.colorScheme.primary else Color.White.copy(alpha = 0.34f),
                shape = itemShape
            )
    } else if (inputHandledByGroup) {
        Modifier
    } else {
        when (descriptor.type) {
            TouchControlType.Button -> Modifier.pointerInteropFilter { event ->
                val controlId = descriptor.controlId ?: return@pointerInteropFilter false
                when (event.actionMasked) {
                    MotionEvent.ACTION_DOWN, MotionEvent.ACTION_POINTER_DOWN -> {
                        val pointerId = event.getPointerId(event.actionIndex)
                        if (pointerOwner.acquire(pointerId) && !pressed) {
                            pressed = true
                            currentOnButtonChange(controlId, true)
                        }
                        true
                    }

                    MotionEvent.ACTION_UP, MotionEvent.ACTION_POINTER_UP -> {
                        val pointerId = event.getPointerId(event.actionIndex)
                        if (pointerOwner.release(pointerId) && pressed) {
                            pressed = false
                            currentOnButtonChange(controlId, false)
                        }
                        true
                    }

                    MotionEvent.ACTION_CANCEL -> {
                        if (pointerOwner.cancel() && pressed) {
                            pressed = false
                            currentOnButtonChange(controlId, false)
                        }
                        true
                    }

                    else -> true
                }
            }

            TouchControlType.TouchSwitch -> Modifier.pointerInteropFilter { event ->
                when (event.actionMasked) {
                    MotionEvent.ACTION_DOWN, MotionEvent.ACTION_POINTER_DOWN -> {
                        val pointerId = event.getPointerId(event.actionIndex)
                        if (pointerOwner.acquire(pointerId)) {
                            pressed = true
                        }
                        true
                    }

                    MotionEvent.ACTION_UP, MotionEvent.ACTION_POINTER_UP -> {
                        val pointerId = event.getPointerId(event.actionIndex)
                        if (pointerOwner.release(pointerId)) {
                            pressed = false
                            onBackTouchToggle()
                        }
                        true
                    }

                    MotionEvent.ACTION_CANCEL -> {
                        if (pointerOwner.cancel()) {
                            pressed = false
                        }
                        true
                    }

                    else -> true
                }
            }

            TouchControlType.Analog -> Modifier
        }
    }

    Box(modifier = sizeModifier.testTag("touch_control_${element.id}").then(inputModifier), contentAlignment = Alignment.Center) {
        when (descriptor.type) {
            TouchControlType.Analog -> {
                if (editMode) {
                    if (element.analogMode == TouchAnalogMode.TouchArea) {
                        StaticAnalogTouchArea(alpha = alpha, visualStyle = visualStyle)
                    } else {
                        StaticAnalogStick(alpha = alpha, visualStyle = visualStyle)
                    }
                } else if (element.analogMode == TouchAnalogMode.TouchArea) {
                    AnalogTouchArea(
                        alpha = alpha,
                        visualStyle = visualStyle,
                        pressEffect = pressEffect,
                        onAxisChange = { x, y ->
                            descriptor.axisX?.let { onAxisChange(it, x) }
                            descriptor.axisY?.let { onAxisChange(it, y) }
                        }
                    )
                } else {
                    AnalogStick(
                        analogSize = with(density) { minOf(widthPx, heightPx).toDp() },
                        alpha = alpha,
                        visualStyle = visualStyle,
                        pressEffect = pressEffect,
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
                    drawableRes = if (descriptor.type == TouchControlType.TouchSwitch && touchMode == 1) {
                        R.drawable.button_touch_b
                    } else {
                        descriptor.drawableRes
                    },
                    width = with(density) { widthPx.toDp() },
                    height = with(density) { heightPx.toDp() },
                    alpha = alpha,
                    shape = descriptor.shape,
                    pressed = !editMode && (pressed || externallyPressed),
                    visualStyle = visualStyle,
                    pressEffect = pressEffect
                )
                element.secondaryActionId?.let(::touchControlDescriptor)?.let { secondary ->
                    Text(
                        text = "+ " + stringResource(secondary.labelRes),
                        color = Color.White,
                        style = MaterialTheme.typography.labelSmall,
                        modifier = Modifier.align(Alignment.BottomCenter)
                            .background(Color.Black.copy(alpha = 0.72f), RoundedCornerShape(4.dp))
                            .padding(horizontal = 3.dp)
                    )
                }
                if (descriptor.type == TouchControlType.TouchSwitch && touchMode == 2) {
                    Text(
                        text = "F+B",
                        color = Color.White.copy(alpha = alpha),
                        style = MaterialTheme.typography.labelSmall,
                        modifier = Modifier
                            .align(Alignment.BottomCenter)
                            .background(Color.Black.copy(alpha = 0.75f), RoundedCornerShape(4.dp))
                            .padding(horizontal = 3.dp)
                    )
                }
            }
        }
    }
}

@Composable
private fun StaticAnalogTouchArea(alpha: Float, visualStyle: TouchControlVisualStyle) {
    val palette = touchVisualPalette(visualStyle)
    Box(
        modifier = Modifier
            .fillMaxSize()
            .graphicsLayer(alpha = alpha)
            .background(palette.fill, RoundedCornerShape(18.dp))
            .border(palette.borderWidth, palette.accent.copy(alpha = 0.70f), RoundedCornerShape(18.dp)),
        contentAlignment = Alignment.Center
    ) {
        Text(
            text = stringResource(R.string.emulation_controls_editor_touch_area_mode),
            style = MaterialTheme.typography.labelLarge.copy(fontWeight = FontWeight.Bold),
            color = palette.accent
        )
    }
}

@Composable
private fun StaticAnalogStick(alpha: Float, visualStyle: TouchControlVisualStyle) {
    val palette = touchVisualPalette(visualStyle)
    Box(
        modifier = Modifier
            .fillMaxSize()
            .graphicsLayer(alpha = alpha)
            .background(palette.fill, CircleShape)
            .border(palette.borderWidth, palette.accent.copy(alpha = 0.64f), CircleShape),
        contentAlignment = Alignment.Center
    ) {
        Image(
            painter = painterResource(R.drawable.ic_controller_analog_base),
            contentDescription = null,
            modifier = Modifier.fillMaxSize(),
            contentScale = ContentScale.Fit,
            colorFilter = palette.colorFilter
        )
        Image(
            painter = painterResource(R.drawable.ic_controller_analog_stick),
            contentDescription = null,
            modifier = Modifier.fillMaxSize(0.56f),
            contentScale = ContentScale.Fit,
            colorFilter = palette.colorFilter
        )
    }
}

internal fun mergeTouchLayout(
    defaults: List<TouchControlElement>,
    saved: List<TouchControlElement>?
): List<TouchControlElement> {
    val savedById = saved.orEmpty().associateBy { it.id }
    val standard = defaults.map { default ->
        val element = savedById[default.id]
        val descriptor = element?.actionId?.let(::touchControlDescriptor)
        if (element != null && descriptor?.type == touchControlDescriptor(default.id)?.type)
            element.coerceToCanvas()
        else default
    }
    val custom = saved.orEmpty().filter {
        it.id.startsWith("custom_") && touchControlDescriptor(it.actionId)?.type == TouchControlType.Button
    }.distinctBy { it.id }.take(32).map { it.coerceToCanvas() }
    return standard + custom
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
    return normalized()
}

@Composable
private fun AssetButton(
    drawableRes: Int,
    width: Dp,
    height: Dp,
    alpha: Float,
    shape: Shape,
    pressed: Boolean,
    visualStyle: TouchControlVisualStyle,
    pressEffect: TouchControlPressEffect,
    modifier: Modifier = Modifier,
    rotation: Float = 0f
) {
    val effectiveVisualStyle = if (
        drawableRes == R.drawable.button_touch_f ||
        drawableRes == R.drawable.button_touch_b
    ) {
        TouchControlVisualStyle.CLASSIC
    } else {
        visualStyle
    }
    VectorOverlayButton(
        drawableRes = drawableRes,
        width = width,
        height = height,
        modifier = modifier.graphicsLayer(rotationZ = rotation),
        shape = shape,
        alpha = alpha,
        pressed = pressed,
        interactive = false,
        visualStyle = effectiveVisualStyle,
        pressEffect = pressEffect
    )
}

@Composable
private fun AnalogTouchArea(
    alpha: Float,
    visualStyle: TouchControlVisualStyle,
    pressEffect: TouchControlPressEffect,
    onAxisChange: (Short, Short) -> Unit,
    modifier: Modifier = Modifier
) {
    val currentOnAxisChange by rememberUpdatedState(onAxisChange)
    var sizePx by remember { mutableStateOf(androidx.compose.ui.geometry.Size.Zero) }
    var startOffset by remember { mutableStateOf(Offset.Zero) }
    var pressed by remember { mutableStateOf(false) }
    var lastX by remember { mutableIntStateOf(0) }
    var lastY by remember { mutableIntStateOf(0) }
    var activePointerId by remember { mutableIntStateOf(MotionEvent.INVALID_POINTER_ID) }

    fun sendAxis(x: Float, y: Float) {
        val quantizedX = (x * Short.MAX_VALUE).roundToInt().coerceIn(Short.MIN_VALUE.toInt(), Short.MAX_VALUE.toInt())
        val quantizedY = (y * Short.MAX_VALUE).roundToInt().coerceIn(Short.MIN_VALUE.toInt(), Short.MAX_VALUE.toInt())
        if (quantizedX == lastX && quantizedY == lastY) return
        lastX = quantizedX
        lastY = quantizedY
        currentOnAxisChange(quantizedX.toShort(), quantizedY.toShort())
    }

    fun resetArea() {
        pressed = false
        activePointerId = MotionEvent.INVALID_POINTER_ID
        sendAxis(0f, 0f)
    }

    fun updateArea(position: Offset) {
        if (sizePx.width == 0f || sizePx.height == 0f) return
        val maxDistance = (minOf(sizePx.width, sizePx.height) * 0.38f).coerceAtLeast(1f)
        val raw = position - startOffset
        val distance = raw.getDistance()
        val clamped = if (distance > maxDistance && distance > 0f) raw * (maxDistance / distance) else raw
        val nx = (clamped.x / maxDistance).coerceIn(-1f, 1f).let { if (abs(it) < 0.08f) 0f else it }
        val ny = (clamped.y / maxDistance).coerceIn(-1f, 1f).let { if (abs(it) < 0.08f) 0f else it }
        sendAxis(nx, ny)
    }

    DisposableEffect(Unit) {
        onDispose {
            if (lastX != 0 || lastY != 0) {
                currentOnAxisChange(0.toShort(), 0.toShort())
            }
        }
    }

    Box(
        modifier = modifier
            .fillMaxSize()
            .onSizeChanged { sizePx = androidx.compose.ui.geometry.Size(it.width.toFloat(), it.height.toFloat()) }
            .pointerInteropFilter { event ->
                when (event.actionMasked) {
                    MotionEvent.ACTION_DOWN,
                    MotionEvent.ACTION_POINTER_DOWN -> {
                        if (activePointerId == MotionEvent.INVALID_POINTER_ID) {
                            val index = event.actionIndex
                            activePointerId = event.getPointerId(index)
                            pressed = true
                            startOffset = Offset(event.getX(index), event.getY(index))
                            sendAxis(0f, 0f)
                        }
                        true
                    }
                    MotionEvent.ACTION_MOVE -> {
                        val index = event.findPointerIndex(activePointerId)
                        if (index >= 0) {
                            updateArea(Offset(event.getX(index), event.getY(index)))
                        }
                        true
                    }
                    MotionEvent.ACTION_UP,
                    MotionEvent.ACTION_POINTER_UP -> {
                        if (event.getPointerId(event.actionIndex) == activePointerId) resetArea()
                        true
                    }
                    MotionEvent.ACTION_CANCEL -> {
                        resetArea()
                        true
                    }
                    else -> true
                }
            }
    ) {
        TouchControlVisualLayer(
            alpha = alpha,
            pressed = pressed,
            visualStyle = visualStyle,
            pressEffect = pressEffect,
            shape = RoundedCornerShape(18.dp),
            modifier = Modifier.fillMaxSize()
        )
    }
}

@Composable
private fun AnalogStick(
    analogSize: Dp,
    alpha: Float,
    visualStyle: TouchControlVisualStyle,
    pressEffect: TouchControlPressEffect,
    onAxisChange: (Short, Short) -> Unit,
    modifier: Modifier = Modifier
) {
    val currentOnAxisChange by rememberUpdatedState(onAxisChange)
    var sizePx by remember { mutableStateOf(androidx.compose.ui.geometry.Size.Zero) }
    var thumbOffset by remember { mutableStateOf(Offset.Zero) }
    var pressed by remember { mutableStateOf(false) }
    var lastX by remember { mutableIntStateOf(0) }
    var lastY by remember { mutableIntStateOf(0) }
    var activePointerId by remember { mutableIntStateOf(MotionEvent.INVALID_POINTER_ID) }

    fun sendAxis(x: Float, y: Float) {
        val quantizedX = (x * Short.MAX_VALUE).roundToInt().coerceIn(Short.MIN_VALUE.toInt(), Short.MAX_VALUE.toInt())
        val quantizedY = (y * Short.MAX_VALUE).roundToInt().coerceIn(Short.MIN_VALUE.toInt(), Short.MAX_VALUE.toInt())
        if (quantizedX == lastX && quantizedY == lastY) return
        lastX = quantizedX
        lastY = quantizedY
        currentOnAxisChange(quantizedX.toShort(), quantizedY.toShort())
    }

    fun resetStick() {
        pressed = false
        activePointerId = MotionEvent.INVALID_POINTER_ID
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

    DisposableEffect(Unit) {
        onDispose {
            if (lastX != 0 || lastY != 0) {
                currentOnAxisChange(0.toShort(), 0.toShort())
            }
        }
    }

    Box(
        modifier = modifier
            .size(analogSize)
            .onSizeChanged { sizePx = androidx.compose.ui.geometry.Size(it.width.toFloat(), it.height.toFloat()) }
            .pointerInteropFilter { event ->
                when (event.actionMasked) {
                    MotionEvent.ACTION_DOWN,
                    MotionEvent.ACTION_POINTER_DOWN -> {
                        if (activePointerId == MotionEvent.INVALID_POINTER_ID) {
                            val index = event.actionIndex
                            activePointerId = event.getPointerId(index)
                            pressed = true
                            updateStick(Offset(event.getX(index), event.getY(index)))
                        }
                        true
                    }
                    MotionEvent.ACTION_MOVE -> {
                        val index = event.findPointerIndex(activePointerId)
                        if (index >= 0) {
                            updateStick(Offset(event.getX(index), event.getY(index)))
                        }
                        true
                    }
                    MotionEvent.ACTION_UP,
                    MotionEvent.ACTION_POINTER_UP -> {
                        if (event.getPointerId(event.actionIndex) == activePointerId) resetStick()
                        true
                    }
                    MotionEvent.ACTION_CANCEL -> {
                        resetStick()
                        true
                    }
                    else -> true
                }
            },
        contentAlignment = Alignment.Center
    ) {
        val maxDistance = minOf(sizePx.width, sizePx.height) * 0.48f
        VectorAnalogStick(
            analogSize = analogSize,
            analogWidth = analogSize,
            analogHeight = analogSize,
            alpha = alpha,
            visualX = if (maxDistance > 0f) thumbOffset.x / maxDistance else 0f,
            visualY = if (maxDistance > 0f) thumbOffset.y / maxDistance else 0f,
            interactive = false,
            visualStyle = visualStyle,
            pressEffect = pressEffect,
            pressed = pressed
        )
    }
}

private data class TouchVisualPalette(
    val accent: Color,
    val fill: Color,
    val borderWidth: Dp,
    val colorFilter: ColorFilter?
)

@Composable
private fun touchVisualPalette(style: TouchControlVisualStyle): TouchVisualPalette = when (style) {
    TouchControlVisualStyle.CLASSIC -> TouchVisualPalette(
        accent = Color.White,
        fill = Color.Transparent,
        borderWidth = 0.dp,
        colorFilter = null
    )
    TouchControlVisualStyle.LEGACY -> TouchVisualPalette(
        accent = Color.White,
        fill = Color.White.copy(alpha = 0.09f),
        borderWidth = 1.dp,
        colorFilter = ColorFilter.tint(Color.White.copy(alpha = 0.94f))
    )
    TouchControlVisualStyle.MODERN -> TouchVisualPalette(
        accent = MaterialTheme.colorScheme.primary,
        fill = MaterialTheme.colorScheme.primary.copy(alpha = 0.16f),
        borderWidth = 2.dp,
        colorFilter = ColorFilter.tint(MaterialTheme.colorScheme.primary)
    )
    TouchControlVisualStyle.ARCADE -> TouchVisualPalette(
        accent = Color(0xFFFFD166),
        fill = Color(0xFF653754).copy(alpha = 0.72f),
        borderWidth = 2.dp,
        colorFilter = ColorFilter.tint(Color(0xFFFFD166))
    )
    TouchControlVisualStyle.MINIMAL -> TouchVisualPalette(
        accent = Color.White.copy(alpha = 0.74f),
        fill = Color.Transparent,
        borderWidth = 1.dp,
        colorFilter = ColorFilter.tint(Color.White.copy(alpha = 0.74f))
    )
}

@Composable
private fun TouchControlVisualLayer(
    alpha: Float,
    pressed: Boolean,
    visualStyle: TouchControlVisualStyle,
    pressEffect: TouchControlPressEffect,
    shape: Shape,
    modifier: Modifier = Modifier
) {
    val palette = touchVisualPalette(visualStyle)
    Box(
        modifier = modifier
            .graphicsLayer(alpha = alpha)
            .background(
                if (pressed && pressEffect == TouchControlPressEffect.GLOW) {
                    palette.accent.copy(alpha = 0.30f)
                } else {
                    palette.fill
                },
                shape
            )
            .border(
                if (pressed && pressEffect == TouchControlPressEffect.GLOW) 3.dp else palette.borderWidth,
                palette.accent.copy(
                    alpha = if (pressed && pressEffect == TouchControlPressEffect.GLOW) 0.95f else 0.66f
                ),
                shape
            )
    )
}

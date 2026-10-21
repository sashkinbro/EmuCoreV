package com.sbro.emucorev.core

import android.content.Context
import android.hardware.input.InputManager
import android.os.Build
import android.os.Handler
import android.os.Looper
import android.util.Log
import android.view.InputDevice
import android.view.KeyEvent
import android.view.MotionEvent
import com.sbro.emucorev.core.input.InputDeviceClassifier
import com.sbro.emucorev.data.AppPreferences
import com.sbro.emucorev.data.KeyboardMouseSettings
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableSharedFlow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharedFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asSharedFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import kotlin.time.Duration.Companion.milliseconds

/**
 * Keyboard and mouse input is deliberately isolated from the gamepad and touch
 * paths. The manager keeps one merged snapshot per emulated pad and injects it
 * through the native bridge, where Vita3K merges it with physical controllers
 * on the controller read path. Releasing one source therefore never clears a
 * button another source still holds.
 */
object KeyboardMouseManager {
    enum class BindingSource {
        KEYBOARD,
        MOUSE
    }

    enum class ShortcutAction {
        QUICK_SAVE,
        QUICK_LOAD
    }

    data class ConnectionState(
        val keyboardConnected: Boolean = false,
        val mouseConnected: Boolean = false
    )

    data class BindingCapture(
        val source: BindingSource,
        val padIndex: Int,
        val actionId: String
    )

    private data class HeldBinding(
        val actionId: String,
        val padIndex: Int,
        val shortcut: Boolean
    )

    private data class PadSnapshot(
        val buttons: Int,
        val leftX: Float,
        val leftY: Float,
        val rightX: Float,
        val rightY: Float
    )

    private const val CONNECTION_POLL_INTERVAL_MS = 500L
    private const val MOUSE_AIM_IDLE_RELEASE_MS = 70L
    private const val PAD_COUNT = 2
    private const val TAG = "EmuCoreVInput"

    private val FINGERPRINT_UINPUT_TOKENS = setOf(
        "uinput-fpc",
        "uinput-goodix",
        "uinput-synaptics",
        "uinput-elan",
        "uinput-vfs",
        "uinput-atrus"
    )

    private val INTERNAL_KEYBOARD_NAMES = setOf(
        "gpio-keys",
        "pmic_resin",
        "pmic_pwrkey",
        "uinput_nav",
        "oplus_fp_input"
    )

    private val SYSTEM_KEY_CODES = setOf(
        KeyEvent.KEYCODE_BACK,
        KeyEvent.KEYCODE_HOME,
        KeyEvent.KEYCODE_APP_SWITCH,
        KeyEvent.KEYCODE_VOLUME_UP,
        KeyEvent.KEYCODE_VOLUME_DOWN,
        KeyEvent.KEYCODE_VOLUME_MUTE,
        KeyEvent.KEYCODE_POWER
    )

    private val MOUSE_BUTTON_MASKS = intArrayOf(
        MotionEvent.BUTTON_PRIMARY,
        MotionEvent.BUTTON_SECONDARY,
        MotionEvent.BUTTON_TERTIARY,
        MotionEvent.BUTTON_BACK,
        MotionEvent.BUTTON_FORWARD
    )

    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.Main.immediate)

    private val _globalSettings = MutableStateFlow(KeyboardMouseSettings())
    val globalSettings: StateFlow<KeyboardMouseSettings> = _globalSettings.asStateFlow()

    private val _gameOverride = MutableStateFlow<KeyboardMouseSettings?>(null)
    val gameOverride: StateFlow<KeyboardMouseSettings?> = _gameOverride.asStateFlow()

    private val _settings = MutableStateFlow(KeyboardMouseSettings())

    /** Effective settings: the active per-game override, or the global preferences. */
    val settings: StateFlow<KeyboardMouseSettings> = _settings.asStateFlow()

    private val _connection = MutableStateFlow(ConnectionState())
    val connection: StateFlow<ConnectionState> = _connection.asStateFlow()

    private val _bindingCapture = MutableStateFlow<BindingCapture?>(null)
    val bindingCapture: StateFlow<BindingCapture?> = _bindingCapture.asStateFlow()

    private val _shortcuts = MutableSharedFlow<ShortcutAction>(extraBufferCapacity = 4)
    val shortcuts: SharedFlow<ShortcutAction> = _shortcuts.asSharedFlow()

    private var captureCallback: ((Int) -> Unit)? = null

    @Volatile
    private var initialized = false

    @Volatile
    private var emulationInputEnabled = false

    @Volatile
    private var pointerCaptureRequested = false
    private var pointerCaptureSuppressed = false
    private var pointerCaptureListener: ((Boolean) -> Unit)? = null

    private val actionsById = KeyboardMouseActions.byId
    private val heldKeyboard = mutableMapOf<Int, HeldBinding>()
    private val heldMouse = mutableMapOf<Int, HeldBinding>()
    private var aimReleaseJob: Job? = null
    private val wheelReleaseJobs = mutableMapOf<String, Job>()
    private val wheelPulsePads = mutableMapOf<String, Int>()
    private val lastSentStates = Array(PAD_COUNT) {
        PadSnapshot(0, 0f, 0f, 0f, 0f)
    }

    private var lastAbsoluteX = Float.NaN
    private var lastAbsoluteY = Float.NaN
    private var smoothedAimX = 0f
    private var smoothedAimY = 0f
    private var lastMouseEventTime = Long.MIN_VALUE
    private var lastMouseAction = -1

    fun ensureInitialized(context: Context) {
        if (initialized) return
        initialized = true
        val prefs = AppPreferences(context.applicationContext)
        _globalSettings.value = prefs.keyboardMouseSettings
        _settings.value = prefs.keyboardMouseSettings
        runCatching {
            val inputManager = context.getSystemService(InputManager::class.java)
            inputManager?.registerInputDeviceListener(
                object : InputManager.InputDeviceListener {
                    override fun onInputDeviceAdded(deviceId: Int) = refreshConnections()

                    override fun onInputDeviceRemoved(deviceId: Int) = refreshConnections()

                    override fun onInputDeviceChanged(deviceId: Int) = refreshConnections()
                },
                Handler(Looper.getMainLooper())
            )
        }.onFailure { error ->
            Log.w(TAG, "Unable to register input-device listener", error)
        }
        scope.launch {
            prefs.keyboardMouseSettingsFlow.collect { newSettings ->
                applyGlobalSettings(newSettings)
            }
        }
        scope.launch {
            while (isActive) {
                runCatching { refreshConnections() }
                    .onFailure { error -> Log.w(TAG, "Input-device poll failed", error) }
                delay(CONNECTION_POLL_INTERVAL_MS.milliseconds)
            }
        }
        refreshConnections()
    }

    /** Forces a fresh device scan; called by the Activity when it regains focus. */
    fun refreshConnectionState() {
        runCatching { refreshConnections() }
            .onFailure { error -> Log.w(TAG, "Explicit input-device refresh failed", error) }
    }

    /** The Activity owns the Android pointer-capture primitive; the manager owns policy. */
    fun setPointerCaptureListener(listener: ((Boolean) -> Unit)?) {
        pointerCaptureListener = listener
        pointerCaptureRequested = false
        updatePointerCapture()
    }

    fun setEmulationInputEnabled(enabled: Boolean) {
        if (emulationInputEnabled == enabled) return
        emulationInputEnabled = enabled
        if (!enabled) {
            releaseAll()
            resetMouseDeltaAnchor()
        } else {
            refreshConnections()
        }
        updatePointerCapture()
    }

    /** Called when the Activity gains window focus so a deferred capture can be retried. */
    fun refreshPointerCapture() {
        pointerCaptureRequested = false
        updatePointerCapture()
    }

    /** Light-gun aiming and settings dialogs need the absolute system pointer. */
    fun setPointerCaptureSuppressed(suppressed: Boolean) {
        if (pointerCaptureSuppressed == suppressed) return
        pointerCaptureSuppressed = suppressed
        pointerCaptureRequested = false
        updatePointerCapture()
    }

    /** Applies a game-specific profile, or null to fall back to the global settings. */
    fun setPerGameOverride(settings: KeyboardMouseSettings?) {
        if (_gameOverride.value == settings) return
        _gameOverride.value = settings
        publishEffectiveSettings()
    }

    fun startKeyboardBindingCapture(
        padIndex: Int,
        actionId: String,
        onCaptured: (Int) -> Unit
    ) {
        captureCallback = onCaptured
        _bindingCapture.value = BindingCapture(
            source = BindingSource.KEYBOARD,
            padIndex = padIndex.coerceIn(0, PAD_COUNT - 1),
            actionId = actionId
        )
    }

    fun startMouseBindingCapture(
        padIndex: Int,
        actionId: String,
        onCaptured: (Int) -> Unit
    ) {
        captureCallback = onCaptured
        _bindingCapture.value = BindingCapture(
            source = BindingSource.MOUSE,
            padIndex = padIndex.coerceIn(0, PAD_COUNT - 1),
            actionId = actionId
        )
    }

    fun cancelBindingCapture() {
        captureCallback = null
        _bindingCapture.value = null
    }

    // ------------------------------------------------------------------
    // Binding capture
    // ------------------------------------------------------------------

    fun handleKeyBindingCapture(event: KeyEvent): Boolean {
        val capture = _bindingCapture.value ?: return false
        if (event.keyCode == KeyEvent.KEYCODE_BACK ||
            (capture.source == BindingSource.MOUSE && event.keyCode == KeyEvent.KEYCODE_ESCAPE)
        ) {
            if (event.action == KeyEvent.ACTION_UP) cancelBindingCapture()
            return true
        }
        if (capture.source != BindingSource.KEYBOARD) return false
        if (event.deviceId >= 0 && InputDeviceClassifier.isPhysicalGameController(event.deviceId)) {
            return false
        }
        if (event.action == KeyEvent.ACTION_UP) return true
        if (event.action != KeyEvent.ACTION_DOWN || event.repeatCount != 0) return true
        if (event.keyCode in SYSTEM_KEY_CODES || event.keyCode == KeyEvent.KEYCODE_UNKNOWN) {
            return true
        }
        val callback = captureCallback
        captureCallback = null
        _bindingCapture.value = null
        callback?.invoke(event.keyCode)
        return true
    }

    fun handleMouseBindingCapture(event: MotionEvent): Boolean {
        val capture = _bindingCapture.value ?: return false
        if (capture.source != BindingSource.MOUSE) return false
        if (!event.isMouseSource()) return false
        if (event.actionMasked == MotionEvent.ACTION_CANCEL) {
            cancelBindingCapture()
            return true
        }
        val button = when (event.actionMasked) {
            MotionEvent.ACTION_BUTTON_PRESS -> mouseButtonForMask(event.actionButton)
            MotionEvent.ACTION_DOWN ->
                firstPressedMouseButton(event.buttonState) ?: KeyboardMouseBindings.MOUSE_BUTTON_LEFT
            else -> null
        } ?: return true
        val callback = captureCallback
        captureCallback = null
        _bindingCapture.value = null
        callback?.invoke(button)
        return true
    }

    // ------------------------------------------------------------------
    // Live keyboard input
    // ------------------------------------------------------------------

    fun handleKeyEvent(event: KeyEvent): Boolean {
        if (!emulationInputEnabled) return false
        if (!_settings.value.keyboardEnabled) return false
        if (event.deviceId >= 0 && InputDeviceClassifier.isPhysicalGameController(event.deviceId)) {
            return false
        }
        if (event.device != null) {
            if (!event.device.supportsSource(InputDevice.SOURCE_KEYBOARD)) return false
        }
        if (event.keyCode in SYSTEM_KEY_CODES) return false

        return when (event.action) {
            KeyEvent.ACTION_DOWN -> {
                if (event.repeatCount != 0) {
                    return heldKeyboard.containsKey(event.keyCode)
                }
                val padIndex = _settings.value.keyboardTargetPad.coerceIn(0, PAD_COUNT - 1)
                val actionId = resolveKeyboardAction(event.keyCode, padIndex) ?: return false
                pressKeyboardKey(event.keyCode, padIndex, actionId)
            }
            KeyEvent.ACTION_UP -> {
                val held = heldKeyboard.remove(event.keyCode) ?: return false
                releaseHeldBinding(held)
                true
            }
            else -> false
        }
    }

    private fun pressKeyboardKey(keyCode: Int, padIndex: Int, actionId: String): Boolean {
        if (heldKeyboard.containsKey(keyCode)) return true
        val action = actionsById[actionId] ?: return false
        heldKeyboard[keyCode] = HeldBinding(actionId, padIndex, action.shortcut)
        if (action.shortcut) {
            emitShortcut(actionId)
            return true
        }
        updatePadState(padIndex)
        return true
    }

    private fun resolveKeyboardAction(keyCode: Int, padIndex: Int): String? {
        val custom = _settings.value.keyboardBindingsByPad[padIndex].orEmpty()
        // Explicit custom bindings win over defaults, including bindings for actions
        // that have no default key at all.
        custom.keys.sorted().forEach { actionId ->
            if (actionId !in actionsById) return@forEach
            if (KeyboardMouseBindings.resolveKeyboardBinding(actionId, custom) == keyCode) {
                return actionId
            }
        }
        return KeyboardMouseBindings.keyboardDefaults.keys
            .firstOrNull { actionId ->
                KeyboardMouseBindings.resolveKeyboardBinding(actionId, custom) == keyCode
            }
    }

    // ------------------------------------------------------------------
    // Live mouse input
    // ------------------------------------------------------------------

    fun handleMouseButtonEvent(
        event: MotionEvent,
        fromCapturedPointer: Boolean = false
    ): Boolean {
        if (!emulationInputEnabled || !_settings.value.mouseEnabled) return false
        if (!fromCapturedPointer && !event.isMouseSource()) return false

        return when (event.actionMasked) {
            MotionEvent.ACTION_BUTTON_PRESS -> {
                val button = mouseButtonForMask(event.actionButton) ?: return false
                pressMouseButton(KeyboardMouseBindings.swapMouseButton(button, _settings.value.mouseLeftHanded))
            }
            MotionEvent.ACTION_BUTTON_RELEASE -> {
                val button = mouseButtonForMask(event.actionButton) ?: return false
                releaseMouseButton(KeyboardMouseBindings.swapMouseButton(button, _settings.value.mouseLeftHanded))
            }
            MotionEvent.ACTION_DOWN -> {
                val button = firstPressedMouseButton(event.buttonState)
                    ?: KeyboardMouseBindings.MOUSE_BUTTON_LEFT
                pressMouseButton(KeyboardMouseBindings.swapMouseButton(button, _settings.value.mouseLeftHanded))
            }
            MotionEvent.ACTION_UP,
            MotionEvent.ACTION_CANCEL -> releaseAllMouseButtons()
            else -> false
        }
    }

    private fun pressMouseButton(button: Int): Boolean {
        if (heldMouse.containsKey(button)) return true
        val padIndex = _settings.value.mouseTargetPad.coerceIn(0, PAD_COUNT - 1)
        val actionId = resolveMouseAction(button, padIndex) ?: return false
        val action = actionsById[actionId] ?: return false
        heldMouse[button] = HeldBinding(actionId, padIndex, action.shortcut)
        if (action.shortcut) {
            emitShortcut(actionId)
            return true
        }
        updatePadState(padIndex)
        return true
    }

    private fun releaseMouseButton(button: Int): Boolean {
        val held = heldMouse.remove(button) ?: return false
        releaseHeldBinding(held)
        return true
    }

    private fun releaseAllMouseButtons(): Boolean {
        if (heldMouse.isEmpty()) return false
        val pads = heldMouse.values.map { it.padIndex }.toSet()
        heldMouse.clear()
        pads.forEach(::updatePadState)
        return true
    }

    fun handleMouseMotionEvent(event: MotionEvent): Boolean {
        if (!event.isMouseSource()) return false
        if (!emulationInputEnabled || !_settings.value.mouseEnabled) return false
        if (_settings.value.mouseAimMode == KeyboardMouseBindings.MOUSE_AIM_OFF) {
            resetMouseDeltaAnchor()
            return false
        }
        if (event.eventTime == lastMouseEventTime && event.actionMasked == lastMouseAction) {
            return true
        }
        lastMouseEventTime = event.eventTime
        lastMouseAction = event.actionMasked

        when (event.actionMasked) {
            MotionEvent.ACTION_HOVER_ENTER,
            MotionEvent.ACTION_HOVER_EXIT,
            MotionEvent.ACTION_OUTSIDE -> {
                resetMouseDeltaAnchor()
                return false
            }
            MotionEvent.ACTION_HOVER_MOVE,
            MotionEvent.ACTION_MOVE -> {
                if (event.isFromSource(InputDevice.SOURCE_MOUSE_RELATIVE)) {
                    if (event.x != 0f || event.y != 0f) applyMouseDelta(event.x, event.y)
                    return true
                }
                val x = event.x
                val y = event.y
                if (lastAbsoluteX.isNaN() || lastAbsoluteY.isNaN()) {
                    lastAbsoluteX = x
                    lastAbsoluteY = y
                    return true
                }
                val dx = x - lastAbsoluteX
                val dy = y - lastAbsoluteY
                lastAbsoluteX = x
                lastAbsoluteY = y
                if (dx != 0f || dy != 0f) applyMouseDelta(dx, dy)
                return true
            }
            else -> return false
        }
    }

    fun handleCapturedPointerEvent(event: MotionEvent): Boolean {
        if (!emulationInputEnabled || !_settings.value.mouseEnabled) return false
        if (event.actionMasked == MotionEvent.ACTION_SCROLL) {
            return handleMouseWheelEvent(event, fromCapturedPointer = true)
        }
        if (event.actionMasked == MotionEvent.ACTION_MOVE) {
            if (_settings.value.mouseAimMode == KeyboardMouseBindings.MOUSE_AIM_OFF) {
                return false
            }
            applyMouseDelta(event.x, event.y)
            return true
        }
        return handleMouseButtonEvent(event, fromCapturedPointer = true)
    }

    fun handleMouseWheelEvent(
        event: MotionEvent,
        fromCapturedPointer: Boolean = false
    ): Boolean {
        if (!emulationInputEnabled || !_settings.value.mouseEnabled) return false
        if (!fromCapturedPointer && !event.isMouseSource()) return false
        if (event.actionMasked != MotionEvent.ACTION_SCROLL) return false
        val delta = event.getAxisValue(MotionEvent.AXIS_VSCROLL)
        if (delta == 0f) return false
        val (upAction, downAction) = KeyboardMouseBindings.mouseWheelActions(
            _settings.value.mouseWheelMode
        )
        val actionId = (if (delta > 0f) upAction else downAction) ?: return false
        if (actionsById[actionId] == null) return false
        pulseWheelAction(actionId)
        return true
    }

    private fun pulseWheelAction(actionId: String) {
        val padIndex = _settings.value.mouseTargetPad.coerceIn(0, PAD_COUNT - 1)
        wheelReleaseJobs.remove(actionId)?.cancel()
        wheelPulsePads[actionId] = padIndex
        updatePadState(padIndex)
        val pulseMs = _settings.value.mouseWheelPulseMs.coerceIn(
            KeyboardMouseBindings.MOUSE_WHEEL_PULSE_MIN_MS,
            KeyboardMouseBindings.MOUSE_WHEEL_PULSE_MAX_MS
        ).toLong()
        wheelReleaseJobs[actionId] = scope.launch {
            delay(pulseMs.milliseconds)
            wheelPulsePads.remove(actionId)
            wheelReleaseJobs.remove(actionId)
            updatePadState(padIndex)
        }
    }

    private fun releaseWheelPulses() {
        if (wheelReleaseJobs.isEmpty() && wheelPulsePads.isEmpty()) return
        val pads = wheelPulsePads.values.toSet()
        wheelReleaseJobs.values.forEach(Job::cancel)
        wheelReleaseJobs.clear()
        wheelPulsePads.clear()
        pads.forEach(::updatePadState)
    }

    private fun applyMouseDelta(dx: Float, dy: Float) {
        val settings = _settings.value
        val (x, y) = KeyboardMouseBindings.normalizeMouseDelta(
            dx = dx,
            dy = dy,
            sensitivityPercent = settings.mouseSensitivity,
            verticalSensitivityPercent = settings.mouseVerticalSensitivity,
            invertX = settings.mouseInvertX,
            invertY = settings.mouseInvertY,
            deadzonePercent = settings.mouseAimDeadzone
        )
        smoothedAimX = KeyboardMouseBindings.smoothAimValue(
            smoothedAimX,
            x,
            settings.mouseSmoothing
        )
        smoothedAimY = KeyboardMouseBindings.smoothAimValue(
            smoothedAimY,
            y,
            settings.mouseSmoothing
        )
        if (smoothedAimX == 0f && smoothedAimY == 0f) return
        updatePadState(settings.mouseTargetPad.coerceIn(0, PAD_COUNT - 1))
        scheduleAimRelease()
    }

    private fun scheduleAimRelease() {
        aimReleaseJob?.cancel()
        aimReleaseJob = scope.launch {
            delay(MOUSE_AIM_IDLE_RELEASE_MS.milliseconds)
            releaseAimAxes()
        }
    }

    private fun releaseAimAxes() {
        val job = aimReleaseJob
        aimReleaseJob = null
        job?.cancel()
        smoothedAimX = 0f
        smoothedAimY = 0f
        updatePadState(_settings.value.mouseTargetPad.coerceIn(0, PAD_COUNT - 1))
    }

    // ------------------------------------------------------------------
    // Native pad snapshots
    // ------------------------------------------------------------------

    private fun updatePadState(padIndex: Int) {
        if (padIndex !in 0 until PAD_COUNT) return
        val snapshot = buildPadSnapshot(padIndex)
        if (snapshot == lastSentStates[padIndex]) return
        if (!NativeLibraryLoader.isNativeSessionInitialized()) return
        val sent = runCatching {
            NativeLib.setKeyboardMousePadState(
                padIndex,
                snapshot.buttons,
                snapshot.leftX,
                snapshot.leftY,
                snapshot.rightX,
                snapshot.rightY
            )
        }.onFailure { error ->
            Log.w(TAG, "Could not publish keyboard/mouse pad state", error)
        }.isSuccess
        if (sent) lastSentStates[padIndex] = snapshot
    }

    private fun buildPadSnapshot(padIndex: Int): PadSnapshot {
        var buttons = 0
        val axes = floatArrayOf(0f, 0f, 0f, 0f)

        fun applyHeld(held: HeldBinding) {
            if (held.padIndex != padIndex || held.shortcut) return
            val action = actionsById[held.actionId] ?: return
            buttons = buttons or action.buttonMask
            if (action.axisIndex in axes.indices) {
                axes[action.axisIndex] += action.axisValue
            }
        }

        heldKeyboard.values.forEach(::applyHeld)
        heldMouse.values.forEach(::applyHeld)

        for ((actionId, pad) in wheelPulsePads) {
            if (pad != padIndex) continue
            val action = actionsById[actionId] ?: continue
            buttons = buttons or action.buttonMask
            if (action.axisIndex in axes.indices) {
                axes[action.axisIndex] += action.axisValue
            }
        }

        val settings = _settings.value
        if (settings.mouseAimMode != KeyboardMouseBindings.MOUSE_AIM_OFF &&
            settings.mouseTargetPad.coerceIn(0, PAD_COUNT - 1) == padIndex
        ) {
            val firstAxis = when (settings.mouseAimMode) {
                KeyboardMouseBindings.MOUSE_AIM_RIGHT_STICK -> 2
                KeyboardMouseBindings.MOUSE_AIM_LEFT_STICK -> 0
                else -> -1
            }
            if (firstAxis >= 0) {
                axes[firstAxis] += smoothedAimX
                axes[firstAxis + 1] += smoothedAimY
            }
        }

        for (index in axes.indices) {
            axes[index] = axes[index].coerceIn(-1f, 1f)
        }
        return PadSnapshot(buttons, axes[0], axes[1], axes[2], axes[3])
    }

    // ------------------------------------------------------------------
    // Cleanup and connection tracking
    // ------------------------------------------------------------------

    fun releaseAll() {
        heldKeyboard.clear()
        heldMouse.clear()
        aimReleaseJob?.cancel()
        aimReleaseJob = null
        wheelReleaseJobs.values.forEach(Job::cancel)
        wheelReleaseJobs.clear()
        wheelPulsePads.clear()
        resetMouseDeltaAnchor()
        for (padIndex in 0 until PAD_COUNT) {
            updatePadState(padIndex)
        }
    }

    private fun releaseHeldBinding(held: HeldBinding) {
        if (held.shortcut) return
        updatePadState(held.padIndex)
    }

    private fun emitShortcut(actionId: String) {
        val shortcut = when (actionId) {
            KeyboardMouseActions.ACTION_QUICK_SAVE -> ShortcutAction.QUICK_SAVE
            KeyboardMouseActions.ACTION_QUICK_LOAD -> ShortcutAction.QUICK_LOAD
            else -> return
        }
        _shortcuts.tryEmit(shortcut)
    }

    private fun refreshConnections() {
        var keyboardConnected = false
        var mouseConnected = false
        for (deviceId in InputDevice.getDeviceIds()) {
            val device = InputDevice.getDevice(deviceId) ?: continue
            if (!keyboardConnected && isUsableKeyboard(device)) keyboardConnected = true
            if (!mouseConnected && isUsableMouse(device)) mouseConnected = true
            if (keyboardConnected && mouseConnected) break
        }
        val updated = ConnectionState(keyboardConnected, mouseConnected)
        val previous = _connection.value
        if (updated == previous) return
        if (previous.keyboardConnected && !updated.keyboardConnected) {
            val pads = heldKeyboard.values.map { it.padIndex }.toSet()
            heldKeyboard.clear()
            pads.forEach(::updatePadState)
        }
        if (previous.mouseConnected && !updated.mouseConnected) {
            val pads = heldMouse.values.map { it.padIndex }.toSet()
            heldMouse.clear()
            releaseWheelPulses()
            releaseAimAxes()
            resetMouseDeltaAnchor()
            pads.forEach(::updatePadState)
        }
        _connection.value = updated
        updatePointerCapture()
    }

    private fun isUsableKeyboard(device: InputDevice): Boolean {
        if (device.isVirtual) return false
        if (device.keyboardType == InputDevice.KEYBOARD_TYPE_NONE) return false
        val sources = device.sources
        if ((sources and InputDevice.SOURCE_KEYBOARD) == 0) return false
        val isGamepadSource =
            (sources and InputDevice.SOURCE_GAMEPAD) == InputDevice.SOURCE_GAMEPAD ||
                (sources and InputDevice.SOURCE_JOYSTICK) == InputDevice.SOURCE_JOYSTICK
        if (isGamepadSource) return false
        val name = device.name.lowercase()
        if (name.contains("system control") || name.contains("consumer control")) return false
        if (FINGERPRINT_UINPUT_TOKENS.any { name.contains(it) }) return false
        if (name in INTERNAL_KEYBOARD_NAMES || name.contains("headset jack")) return false
        if (name.startsWith("uinput")) return false
        val hasTypingKeys = device.hasKeys(
            KeyEvent.KEYCODE_A,
            KeyEvent.KEYCODE_SPACE,
            KeyEvent.KEYCODE_1
        ).all { it }
        val isExternal = Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q && device.isExternal
        return hasTypingKeys || (isExternal && name.isNotBlank())
    }

    private fun isUsableMouse(device: InputDevice): Boolean {
        if (device.isVirtual) return false
        val sources = device.sources
        val hasMouseSource =
            (sources and InputDevice.SOURCE_MOUSE) == InputDevice.SOURCE_MOUSE ||
                (sources and InputDevice.SOURCE_MOUSE_RELATIVE) ==
                InputDevice.SOURCE_MOUSE_RELATIVE
        if (!hasMouseSource) return false
        if ((sources and InputDevice.SOURCE_TOUCHSCREEN) == InputDevice.SOURCE_TOUCHSCREEN) {
            return false
        }
        val name = device.name.lowercase()
        if (name.startsWith("uinput")) return false
        if (name.contains("touchscreen") || name.contains("headset jack")) return false
        if (FINGERPRINT_UINPUT_TOKENS.any { name.contains(it) }) return false
        return true
    }

    private fun applyGlobalSettings(newSettings: KeyboardMouseSettings) {
        if (_globalSettings.value == newSettings) return
        _globalSettings.value = newSettings
        if (_gameOverride.value == null) publishEffectiveSettings()
    }

    private fun publishEffectiveSettings() {
        val newSettings = _gameOverride.value ?: _globalSettings.value
        val previous = _settings.value
        if (previous == newSettings) {
            updatePointerCapture()
            return
        }
        val bindingsChanged =
            previous.keyboardBindingsByPad != newSettings.keyboardBindingsByPad ||
                previous.mouseBindingsByPad != newSettings.mouseBindingsByPad ||
                previous.keyboardTargetPad != newSettings.keyboardTargetPad ||
                previous.mouseTargetPad != newSettings.mouseTargetPad ||
                previous.mouseLeftHanded != newSettings.mouseLeftHanded ||
                previous.mouseWheelMode != newSettings.mouseWheelMode
        val enabledChanged =
            previous.keyboardEnabled != newSettings.keyboardEnabled ||
                previous.mouseEnabled != newSettings.mouseEnabled
        if (bindingsChanged || enabledChanged) releaseAll()
        if (previous.mouseAimMode != newSettings.mouseAimMode) releaseAimAxes()
        _settings.value = newSettings
        updatePointerCapture()
    }

    private fun updatePointerCapture() {
        val settings = _settings.value
        val shouldCapture = emulationInputEnabled &&
            settings.mouseEnabled &&
            settings.mouseAimMode != KeyboardMouseBindings.MOUSE_AIM_OFF &&
            settings.mouseCaptureCursor &&
            !pointerCaptureSuppressed &&
            _connection.value.mouseConnected
        if (shouldCapture == pointerCaptureRequested) return
        pointerCaptureRequested = shouldCapture
        resetMouseDeltaAnchor()
        pointerCaptureListener?.invoke(shouldCapture)
    }

    private fun resetMouseDeltaAnchor() {
        lastAbsoluteX = Float.NaN
        lastAbsoluteY = Float.NaN
        smoothedAimX = 0f
        smoothedAimY = 0f
        lastMouseEventTime = Long.MIN_VALUE
        lastMouseAction = -1
    }

    private fun resolveMouseAction(button: Int, padIndex: Int): String? {
        val custom = _settings.value.mouseBindingsByPad[padIndex].orEmpty()
        custom.entries.sortedBy { it.key }.forEach { (actionId, _) ->
            if (actionId !in actionsById) return@forEach
            if (KeyboardMouseBindings.resolveMouseBinding(actionId, custom) == button) {
                return actionId
            }
        }
        return KeyboardMouseBindings.mouseDefaults.entries
            .firstOrNull { (actionId, code) ->
                code == button && KeyboardMouseBindings.resolveMouseBinding(actionId, custom) == code
            }
            ?.key
    }

    private fun MotionEvent.isMouseSource(): Boolean =
        isFromSource(InputDevice.SOURCE_MOUSE) ||
            isFromSource(InputDevice.SOURCE_MOUSE_RELATIVE)

    private fun mouseButtonForMask(mask: Int): Int? = when (mask) {
        MotionEvent.BUTTON_PRIMARY -> KeyboardMouseBindings.MOUSE_BUTTON_LEFT
        MotionEvent.BUTTON_SECONDARY -> KeyboardMouseBindings.MOUSE_BUTTON_RIGHT
        MotionEvent.BUTTON_TERTIARY -> KeyboardMouseBindings.MOUSE_BUTTON_MIDDLE
        MotionEvent.BUTTON_BACK -> KeyboardMouseBindings.MOUSE_BUTTON_BACK
        MotionEvent.BUTTON_FORWARD -> KeyboardMouseBindings.MOUSE_BUTTON_FORWARD
        else -> null
    }

    private fun firstPressedMouseButton(buttonState: Int): Int? {
        for (mask in MOUSE_BUTTON_MASKS) {
            if ((buttonState and mask) != 0) return mouseButtonForMask(mask)
        }
        return null
    }
}

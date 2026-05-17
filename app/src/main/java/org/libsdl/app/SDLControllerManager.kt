@file:Suppress("DEPRECATION")

package org.libsdl.app

import android.content.Context
import android.os.Build
import android.os.SystemClock
import android.os.VibrationEffect
import android.os.Vibrator
import android.os.VibratorManager
import android.view.InputDevice
import android.view.KeyEvent
import android.view.MotionEvent
import android.view.View
import com.sbro.emucorev.core.VitaCoreConfig
import com.sbro.emucorev.core.VitaCoreConfigRepository
import com.sbro.emucorev.core.VitaGameSettingsRepository
import com.sbro.emucorev.core.vita.Emulator
import com.sbro.emucorev.core.input.InputDeviceClassifier
import java.util.Collections
import java.util.Comparator
import kotlin.math.abs
import kotlin.math.sign

class SDLControllerManager {
    companion object {
        private var mJoystickHandler: SDLJoystickHandler? = null
        private var mHapticHandler: SDLHapticHandler? = null

        @JvmStatic
        external fun nativeSetupJNI(): Int

        @JvmStatic
        external fun nativeAddJoystick(
            deviceId: Int,
            name: String,
            desc: String,
            vendorId: Int,
            productId: Int,
            buttonMask: Int,
            naxes: Int,
            axisMask: Int,
            nhats: Int,
            canRumble: Boolean
        )

        @JvmStatic
        external fun nativeRemoveJoystick(deviceId: Int)

        @JvmStatic
        external fun nativeAddHaptic(deviceId: Int, name: String)

        @JvmStatic
        external fun nativeRemoveHaptic(deviceId: Int)

        @JvmStatic
        external fun onNativePadDown(deviceId: Int, keycode: Int): Boolean

        @JvmStatic
        external fun onNativePadUp(deviceId: Int, keycode: Int): Boolean

        @JvmStatic
        external fun onNativeJoy(deviceId: Int, axis: Int, value: Float)

        @JvmStatic
        external fun onNativeHat(deviceId: Int, hatId: Int, x: Int, y: Int)

        @JvmStatic
        fun handlePadDown(deviceId: Int, keycode: Int): Boolean {
            return onNativePadDown(deviceId, GamepadRuntimeInputSettings.mapButton(keycode))
        }

        @JvmStatic
        fun handlePadUp(deviceId: Int, keycode: Int): Boolean {
            return onNativePadUp(deviceId, GamepadRuntimeInputSettings.mapButton(keycode))
        }

        @JvmStatic
        fun initialize() {
            if (mJoystickHandler == null) {
                mJoystickHandler = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.KITKAT) {
                    SDLJoystickHandler_API19()
                } else {
                    SDLJoystickHandler_API16()
                }
            }

            if (mHapticHandler == null) {
                mHapticHandler = when {
                    Build.VERSION.SDK_INT >= Build.VERSION_CODES.S -> SDLHapticHandler_API31()
                    Build.VERSION.SDK_INT >= Build.VERSION_CODES.O -> SDLHapticHandler_API26()
                    else -> SDLHapticHandler()
                }
            }
        }

        @JvmStatic
        fun handleJoystickMotionEvent(event: MotionEvent): Boolean {
            return mJoystickHandler?.handleMotionEvent(event) ?: false
        }

        @JvmStatic
        fun pollInputDevices() {
            mJoystickHandler?.pollInputDevices()
        }

        @JvmStatic
        fun pollHapticDevices() {
            mHapticHandler?.pollHapticDevices()
        }

        @JvmStatic
        fun hapticRun(deviceId: Int, intensity: Float, length: Int) {
            if (!GamepadRuntimeInputSettings.vibrationEnabled()) return
            mHapticHandler?.run(deviceId, intensity, length)
        }

        @JvmStatic
        fun hapticRumble(deviceId: Int, lowFrequencyIntensity: Float, highFrequencyIntensity: Float, length: Int) {
            if (!GamepadRuntimeInputSettings.vibrationEnabled()) return
            mHapticHandler?.rumble(deviceId, lowFrequencyIntensity, highFrequencyIntensity, length)
        }

        @JvmStatic
        fun hapticStop(deviceId: Int) {
            mHapticHandler?.stop(deviceId)
        }

        @JvmStatic
        fun isDeviceSDLJoystick(deviceId: Int): Boolean {
            return InputDeviceClassifier.isPhysicalGameController(InputDevice.getDevice(deviceId))
        }
    }
}

open class SDLJoystickHandler {
    open fun handleMotionEvent(event: MotionEvent): Boolean = false
    open fun pollInputDevices() {
    }
}

open class SDLJoystickHandler_API16 : SDLJoystickHandler() {
    class SDLJoystick {
        var deviceId: Int = 0
        var name: String = ""
        var desc: String = ""
        var axes: ArrayList<InputDevice.MotionRange> = arrayListOf()
        var hats: ArrayList<InputDevice.MotionRange> = arrayListOf()
    }

    class RangeComparator : Comparator<InputDevice.MotionRange> {
        override fun compare(arg0: InputDevice.MotionRange, arg1: InputDevice.MotionRange): Int {
            var arg0Axis = arg0.axis
            var arg1Axis = arg1.axis
            if (arg0Axis == MotionEvent.AXIS_GAS) {
                arg0Axis = MotionEvent.AXIS_BRAKE
            } else if (arg0Axis == MotionEvent.AXIS_BRAKE) {
                arg0Axis = MotionEvent.AXIS_GAS
            }
            if (arg1Axis == MotionEvent.AXIS_GAS) {
                arg1Axis = MotionEvent.AXIS_BRAKE
            } else if (arg1Axis == MotionEvent.AXIS_BRAKE) {
                arg1Axis = MotionEvent.AXIS_GAS
            }

            if (arg0Axis == MotionEvent.AXIS_Z) {
                arg0Axis = MotionEvent.AXIS_RZ - 1
            } else if (arg0Axis > MotionEvent.AXIS_Z && arg0Axis < MotionEvent.AXIS_RZ) {
                --arg0Axis
            }
            if (arg1Axis == MotionEvent.AXIS_Z) {
                arg1Axis = MotionEvent.AXIS_RZ - 1
            } else if (arg1Axis > MotionEvent.AXIS_Z && arg1Axis < MotionEvent.AXIS_RZ) {
                --arg1Axis
            }

            return arg0Axis - arg1Axis
        }
    }

    private val mJoysticks = ArrayList<SDLJoystick>()

    override fun pollInputDevices() {
        val deviceIds = InputDevice.getDeviceIds()

        deviceIds.forEach { deviceId ->
            if (SDLControllerManager.isDeviceSDLJoystick(deviceId)) {
                var joystick = getJoystick(deviceId)
                if (joystick == null) {
                    val joystickDevice = InputDevice.getDevice(deviceId) ?: return@forEach
                    joystick = SDLJoystick().apply {
                        this.deviceId = deviceId
                        name = joystickDevice.name
                        desc = getJoystickDescriptor(joystickDevice)
                    }

                    val ranges = joystickDevice.motionRanges.toMutableList()
                    Collections.sort(ranges, RangeComparator())
                    ranges.forEach { range ->
                        if ((range.source and InputDevice.SOURCE_CLASS_JOYSTICK) != 0) {
                            if (range.axis == MotionEvent.AXIS_HAT_X || range.axis == MotionEvent.AXIS_HAT_Y) {
                                joystick.hats.add(range)
                            } else {
                                joystick.axes.add(range)
                            }
                        }
                    }

                    var canRumble = false
                    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
                        val manager = joystickDevice.vibratorManager
                        canRumble = manager.vibratorIds.isNotEmpty()
                    }

                    mJoysticks.add(joystick)
                    SDLControllerManager.nativeAddJoystick(
                        joystick.deviceId,
                        joystick.name,
                        joystick.desc,
                        getVendorId(joystickDevice),
                        getProductId(joystickDevice),
                        getButtonMask(joystickDevice),
                        joystick.axes.size,
                        getAxisMask(joystick.axes),
                        joystick.hats.size / 2,
                        canRumble
                    )
                }
            }
        }

        val removedDevices = arrayListOf<Int>()
        mJoysticks.forEach { joystick ->
            if (!deviceIds.contains(joystick.deviceId) ||
                !SDLControllerManager.isDeviceSDLJoystick(joystick.deviceId)
            ) {
                removedDevices.add(joystick.deviceId)
            }
        }

        removedDevices.forEach { deviceId ->
            SDLControllerManager.nativeRemoveJoystick(deviceId)
            mJoysticks.removeAll { it.deviceId == deviceId }
        }
    }

    protected fun getJoystick(deviceId: Int): SDLJoystick? {
        return mJoysticks.firstOrNull { it.deviceId == deviceId }
    }

    override fun handleMotionEvent(event: MotionEvent): Boolean {
        val actionPointerIndex = event.actionIndex
        if (event.actionMasked == MotionEvent.ACTION_MOVE) {
            val joystick = getJoystick(event.deviceId)
            if (joystick != null) {
                joystick.axes.forEachIndexed { index, range ->
                    val targetIndex = GamepadRuntimeInputSettings.targetAxisIndex(range.axis, joystick.axes, index)
                    val value = if (range.range > 0f) {
                        (event.getAxisValue(range.axis, actionPointerIndex) - range.min) / range.range * 2.0f - 1.0f
                    } else {
                        event.getAxisValue(range.axis, actionPointerIndex)
                    }
                    SDLControllerManager.onNativeJoy(
                        joystick.deviceId,
                        targetIndex,
                        GamepadRuntimeInputSettings.mapAxis(range.axis, value)
                    )
                }
                for (i in 0 until joystick.hats.size / 2) {
                    val hatX = event.getAxisValue(joystick.hats[2 * i].axis, actionPointerIndex).toInt().let { kotlin.math.round(it.toFloat()).toInt() }
                    val hatY = event.getAxisValue(joystick.hats[2 * i + 1].axis, actionPointerIndex).toInt().let { kotlin.math.round(it.toFloat()).toInt() }
                    SDLControllerManager.onNativeHat(joystick.deviceId, i, hatX, hatY)
                }
            }
        }
        return true
    }

    open fun getJoystickDescriptor(joystickDevice: InputDevice): String {
        val desc = joystickDevice.descriptor
        return if (!desc.isNullOrEmpty()) desc else joystickDevice.name
    }

    open fun getProductId(joystickDevice: InputDevice): Int = 0
    open fun getVendorId(joystickDevice: InputDevice): Int = 0
    open fun getAxisMask(ranges: List<InputDevice.MotionRange>): Int = -1
    open fun getButtonMask(joystickDevice: InputDevice): Int = -1
}

private object GamepadRuntimeInputSettings {
    private const val CACHE_MS = 500L
    private var cachedAtMs = 0L
    private var cached = Values()

    private data class Values(
        val deadzone: Float = 0.15f,
        val triggerThreshold: Float = 0.12f,
        val buttonProfile: String = VitaCoreConfig.GAMEPAD_PROFILE_STANDARD,
        val vibration: Boolean = true,
        val swapSticks: Boolean = false,
        val invertLeftX: Boolean = false,
        val invertLeftY: Boolean = false,
        val invertRightX: Boolean = false,
        val invertRightY: Boolean = false,
        val analogMultiplier: Float = 1.0f
    )

    fun vibrationEnabled(): Boolean = current().vibration

    fun mapButton(keycode: Int): Int {
        return when (current().buttonProfile) {
            VitaCoreConfig.GAMEPAD_PROFILE_SWAP_CROSS_CIRCLE -> when (keycode) {
                KeyEvent.KEYCODE_BUTTON_A -> KeyEvent.KEYCODE_BUTTON_B
                KeyEvent.KEYCODE_BUTTON_B -> KeyEvent.KEYCODE_BUTTON_A
                else -> keycode
            }
            VitaCoreConfig.GAMEPAD_PROFILE_NINTENDO_FACE -> when (keycode) {
                KeyEvent.KEYCODE_BUTTON_A -> KeyEvent.KEYCODE_BUTTON_B
                KeyEvent.KEYCODE_BUTTON_B -> KeyEvent.KEYCODE_BUTTON_A
                KeyEvent.KEYCODE_BUTTON_X -> KeyEvent.KEYCODE_BUTTON_Y
                KeyEvent.KEYCODE_BUTTON_Y -> KeyEvent.KEYCODE_BUTTON_X
                else -> keycode
            }
            else -> keycode
        }
    }

    fun targetAxisIndex(axis: Int, axes: List<InputDevice.MotionRange>, fallback: Int): Int {
        if (!current().swapSticks) return fallback
        val targetAxis = when (axis) {
            MotionEvent.AXIS_X -> MotionEvent.AXIS_Z
            MotionEvent.AXIS_Y -> MotionEvent.AXIS_RZ
            MotionEvent.AXIS_Z -> MotionEvent.AXIS_X
            MotionEvent.AXIS_RZ -> MotionEvent.AXIS_Y
            else -> axis
        }
        return axes.indexOfFirst { it.axis == targetAxis }.takeIf { it >= 0 } ?: fallback
    }

    fun mapAxis(axis: Int, value: Float): Float {
        val settings = current()
        val isStick = axis == MotionEvent.AXIS_X ||
            axis == MotionEvent.AXIS_Y ||
            axis == MotionEvent.AXIS_Z ||
            axis == MotionEvent.AXIS_RZ
        val isTrigger = axis == MotionEvent.AXIS_LTRIGGER ||
            axis == MotionEvent.AXIS_RTRIGGER ||
            axis == MotionEvent.AXIS_GAS ||
            axis == MotionEvent.AXIS_BRAKE

        val thresholded = when {
            isStick -> applyDeadzone(value, settings.deadzone) * settings.analogMultiplier
            isTrigger -> {
                val amount = ((value + 1f) / 2f).coerceIn(0f, 1f)
                if (amount < settings.triggerThreshold) -1f else value
            }
            else -> value
        }.coerceIn(-1f, 1f)

        val invert = when (axis) {
            MotionEvent.AXIS_X -> if (settings.swapSticks) settings.invertRightX else settings.invertLeftX
            MotionEvent.AXIS_Y -> if (settings.swapSticks) settings.invertRightY else settings.invertLeftY
            MotionEvent.AXIS_Z -> if (settings.swapSticks) settings.invertLeftX else settings.invertRightX
            MotionEvent.AXIS_RZ -> if (settings.swapSticks) settings.invertLeftY else settings.invertRightY
            else -> false
        }
        return if (invert) -thresholded else thresholded
    }

    private fun applyDeadzone(value: Float, deadzone: Float): Float {
        val magnitude = abs(value)
        if (magnitude <= deadzone) return 0f
        return ((magnitude - deadzone) / (1f - deadzone).coerceAtLeast(0.01f)) * value.sign
    }

    private fun current(): Values {
        val now = SystemClock.elapsedRealtime()
        if (now - cachedAtMs < CACHE_MS) return cached
        cachedAtMs = now
        cached = runCatching {
            val context = SDL.getContext()
            val gameId = (context as? Emulator)?.currentGameIdOrIntent().orEmpty()
            val config = if (gameId.isNotBlank()) {
                VitaGameSettingsRepository(context).loadEffective(gameId)
            } else {
                VitaCoreConfigRepository(context).load()
            }
            Values(
                deadzone = config.gamepadDeadzone,
                triggerThreshold = config.gamepadTriggerThreshold,
                buttonProfile = config.gamepadButtonProfile,
                vibration = config.gamepadVibration,
                swapSticks = config.gamepadSwapSticks,
                invertLeftX = config.gamepadInvertLeftX,
                invertLeftY = config.gamepadInvertLeftY,
                invertRightX = config.gamepadInvertRightX,
                invertRightY = config.gamepadInvertRightY,
                analogMultiplier = config.analogMultiplier
            )
        }.getOrDefault(cached)
        return cached
    }
}

class SDLJoystickHandler_API19 : SDLJoystickHandler_API16() {
    override fun getProductId(joystickDevice: InputDevice): Int = joystickDevice.productId
    override fun getVendorId(joystickDevice: InputDevice): Int = joystickDevice.vendorId

    override fun getAxisMask(ranges: List<InputDevice.MotionRange>): Int {
        var axisMask = 0
        if (ranges.size >= 2) axisMask = axisMask or 0x0003
        if (ranges.size >= 4) axisMask = axisMask or 0x000c
        if (ranges.size >= 6) axisMask = axisMask or 0x0030

        var haveZ = false
        var havePastZBeforeRz = false
        ranges.forEach { range ->
            val axis = range.axis
            if (axis == MotionEvent.AXIS_Z) {
                haveZ = true
            } else if (axis > MotionEvent.AXIS_Z && axis < MotionEvent.AXIS_RZ) {
                havePastZBeforeRz = true
            }
        }
        if (haveZ && havePastZBeforeRz) {
            axisMask = axisMask or 0x8000
        }
        return axisMask
    }

    override fun getButtonMask(joystickDevice: InputDevice): Int {
        var buttonMask = 0
        val keys = intArrayOf(
            KeyEvent.KEYCODE_BUTTON_A,
            KeyEvent.KEYCODE_BUTTON_B,
            KeyEvent.KEYCODE_BUTTON_X,
            KeyEvent.KEYCODE_BUTTON_Y,
            KeyEvent.KEYCODE_BACK,
            KeyEvent.KEYCODE_MENU,
            KeyEvent.KEYCODE_BUTTON_MODE,
            KeyEvent.KEYCODE_BUTTON_START,
            KeyEvent.KEYCODE_BUTTON_THUMBL,
            KeyEvent.KEYCODE_BUTTON_THUMBR,
            KeyEvent.KEYCODE_BUTTON_L1,
            KeyEvent.KEYCODE_BUTTON_R1,
            KeyEvent.KEYCODE_DPAD_UP,
            KeyEvent.KEYCODE_DPAD_DOWN,
            KeyEvent.KEYCODE_DPAD_LEFT,
            KeyEvent.KEYCODE_DPAD_RIGHT,
            KeyEvent.KEYCODE_BUTTON_SELECT,
            KeyEvent.KEYCODE_DPAD_CENTER,
            KeyEvent.KEYCODE_BUTTON_L2,
            KeyEvent.KEYCODE_BUTTON_R2,
            KeyEvent.KEYCODE_BUTTON_C,
            KeyEvent.KEYCODE_BUTTON_Z,
            KeyEvent.KEYCODE_BUTTON_1,
            KeyEvent.KEYCODE_BUTTON_2,
            KeyEvent.KEYCODE_BUTTON_3,
            KeyEvent.KEYCODE_BUTTON_4,
            KeyEvent.KEYCODE_BUTTON_5,
            KeyEvent.KEYCODE_BUTTON_6,
            KeyEvent.KEYCODE_BUTTON_7,
            KeyEvent.KEYCODE_BUTTON_8,
            KeyEvent.KEYCODE_BUTTON_9,
            KeyEvent.KEYCODE_BUTTON_10,
            KeyEvent.KEYCODE_BUTTON_11,
            KeyEvent.KEYCODE_BUTTON_12,
            KeyEvent.KEYCODE_BUTTON_13,
            KeyEvent.KEYCODE_BUTTON_14,
            KeyEvent.KEYCODE_BUTTON_15,
            KeyEvent.KEYCODE_BUTTON_16
        )
        val masks = intArrayOf(
            (1 shl 0),
            (1 shl 1),
            (1 shl 2),
            (1 shl 3),
            (1 shl 4),
            (1 shl 6),
            (1 shl 5),
            (1 shl 6),
            (1 shl 7),
            (1 shl 8),
            (1 shl 9),
            (1 shl 10),
            (1 shl 11),
            (1 shl 12),
            (1 shl 13),
            (1 shl 14),
            (1 shl 4),
            (1 shl 0),
            (1 shl 15),
            (1 shl 16),
            (1 shl 17),
            (1 shl 18),
            (1 shl 20),
            (1 shl 21),
            (1 shl 22),
            (1 shl 23),
            (1 shl 24),
            (1 shl 25),
            (1 shl 26),
            (1 shl 27),
            (1 shl 28),
            (1 shl 29),
            (1 shl 30),
            (1 shl 31),
            -1,
            -1,
            -1,
            -1
        )
        val hasKeys = joystickDevice.hasKeys(*keys)
        for (i in keys.indices) {
            if (hasKeys[i]) {
                buttonMask = buttonMask or masks[i]
            }
        }
        return buttonMask
    }
}

open class SDLHapticHandler {
    class SDLHaptic {
        var deviceId: Int = 0
        var name: String = ""
        lateinit var vib: Vibrator
    }

    private val mHaptics = arrayListOf<SDLHaptic>()

    open fun run(deviceId: Int, intensity: Float, length: Int) {
        getHaptic(deviceId)?.vib?.vibrate(length.toLong())
    }

    open fun rumble(deviceId: Int, lowFrequencyIntensity: Float, highFrequencyIntensity: Float, length: Int) {
    }

    open fun stop(deviceId: Int) {
        getHaptic(deviceId)?.vib?.cancel()
    }

    open fun pollHapticDevices() {
        val deviceIdVibratorService = 999999
        var hasVibratorService = false

        val vib = SDL.getContext().getSystemService(Context.VIBRATOR_SERVICE) as? Vibrator
        if (vib != null) {
            hasVibratorService = vib.hasVibrator()
            if (hasVibratorService) {
                var haptic = getHaptic(deviceIdVibratorService)
                if (haptic == null) {
                    haptic = SDLHaptic().apply {
                        deviceId = deviceIdVibratorService
                        name = "VIBRATOR_SERVICE"
                        this.vib = vib
                    }
                    mHaptics.add(haptic)
                    SDLControllerManager.nativeAddHaptic(haptic.deviceId, haptic.name)
                }
            }
        }

        val removedDevices = arrayListOf<Int>()
        mHaptics.forEach { haptic ->
            if (haptic.deviceId != deviceIdVibratorService || !hasVibratorService) {
                removedDevices.add(haptic.deviceId)
            }
        }

        removedDevices.forEach { deviceId ->
            SDLControllerManager.nativeRemoveHaptic(deviceId)
            mHaptics.removeAll { it.deviceId == deviceId }
        }
    }

    protected fun getHaptic(deviceId: Int): SDLHaptic? {
        return mHaptics.firstOrNull { it.deviceId == deviceId }
    }
}

class SDLHapticHandler_API26 : SDLHapticHandler() {
    override fun run(deviceId: Int, intensity: Float, length: Int) {
        val haptic = getHaptic(deviceId) ?: return
        if (intensity == 0.0f) {
            stop(deviceId)
            return
        }

        var vibeValue = kotlin.math.round(intensity * 255).toInt()
        if (vibeValue > 255) vibeValue = 255
        if (vibeValue < 1) {
            stop(deviceId)
            return
        }
        try {
            haptic.vib.vibrate(VibrationEffect.createOneShot(length.toLong(), vibeValue))
        } catch (_: Exception) {
            haptic.vib.vibrate(length.toLong())
        }
    }
}

class SDLHapticHandler_API31 : SDLHapticHandler() {
    override fun run(deviceId: Int, intensity: Float, length: Int) {
        getHaptic(deviceId)?.let { vibrate(it.vib, intensity, length) }
    }

    override fun rumble(deviceId: Int, lowFrequencyIntensity: Float, highFrequencyIntensity: Float, length: Int) {
        val device = InputDevice.getDevice(deviceId) ?: return
        val manager = device.vibratorManager
        val vibrators = manager.vibratorIds
        when {
            vibrators.size >= 2 -> {
                vibrate(manager.getVibrator(vibrators[0]), lowFrequencyIntensity, length)
                vibrate(manager.getVibrator(vibrators[1]), highFrequencyIntensity, length)
            }
            vibrators.size == 1 -> {
                val intensity = (lowFrequencyIntensity * 0.6f) + (highFrequencyIntensity * 0.4f)
                vibrate(manager.getVibrator(vibrators[0]), intensity, length)
            }
        }
    }

    private fun vibrate(vibrator: Vibrator, intensity: Float, length: Int) {
        if (intensity == 0.0f) {
            vibrator.cancel()
            return
        }

        var value = kotlin.math.round(intensity * 255).toInt()
        if (value > 255) value = 255
        if (value < 1) {
            vibrator.cancel()
            return
        }
        try {
            vibrator.vibrate(VibrationEffect.createOneShot(length.toLong(), value))
        } catch (_: Exception) {
            vibrator.vibrate(length.toLong())
        }
    }
}

open class SDLGenericMotionListener_API14 : View.OnGenericMotionListener {
    override fun onGenericMotion(v: View, event: MotionEvent): Boolean {
        if ((event.source and InputDevice.SOURCE_CLASS_JOYSTICK) == InputDevice.SOURCE_CLASS_JOYSTICK &&
            SDLControllerManager.isDeviceSDLJoystick(event.deviceId)
        ) {
            return SDLControllerManager.handleJoystickMotionEvent(event)
        }

        val action = event.actionMasked
        val pointerCount = event.pointerCount
        var consumed = false

        for (i in 0 until pointerCount) {
            val toolType = event.getToolType(i)
            if (toolType == MotionEvent.TOOL_TYPE_MOUSE) {
                when (action) {
                    MotionEvent.ACTION_SCROLL -> {
                        val x = event.getAxisValue(MotionEvent.AXIS_HSCROLL, i)
                        val y = event.getAxisValue(MotionEvent.AXIS_VSCROLL, i)
                        SDLActivity.onNativeMouse(0, action, x, y, false)
                        consumed = true
                    }
                    MotionEvent.ACTION_HOVER_MOVE -> {
                        val x = getEventX(event, i)
                        val y = getEventY(event, i)
                        SDLActivity.onNativeMouse(0, action, x, y, checkRelativeEvent(event))
                        consumed = true
                    }
                }
            } else if (toolType == MotionEvent.TOOL_TYPE_STYLUS || toolType == MotionEvent.TOOL_TYPE_ERASER) {
                when (action) {
                    MotionEvent.ACTION_HOVER_ENTER,
                    MotionEvent.ACTION_HOVER_MOVE,
                    MotionEvent.ACTION_HOVER_EXIT -> {
                        val x = event.getX(i)
                        val y = event.getY(i)
                        var p = event.getPressure(i)
                        if (p > 1.0f) p = 1.0f
                        val buttons = (event.buttonState shr 4) or (1 shl if (toolType == MotionEvent.TOOL_TYPE_STYLUS) 0 else 30)
                        SDLActivity.onNativePen(event.getPointerId(i), buttons, action, x, y, p)
                        consumed = true
                    }
                }
            }
        }

        return consumed
    }

    open fun supportsRelativeMouse(): Boolean = false
    open fun inRelativeMode(): Boolean = false
    open fun setRelativeMouseEnabled(enabled: Boolean): Boolean = false
    open fun reclaimRelativeMouseModeIfNeeded() {
    }
    open fun checkRelativeEvent(event: MotionEvent): Boolean = inRelativeMode()
    open fun getEventX(event: MotionEvent, pointerIndex: Int): Float = event.getX(pointerIndex)
    open fun getEventY(event: MotionEvent, pointerIndex: Int): Float = event.getY(pointerIndex)
}

open class SDLGenericMotionListener_API24 : SDLGenericMotionListener_API14() {
    private var mRelativeModeEnabled = false

    override fun supportsRelativeMouse(): Boolean = true
    override fun inRelativeMode(): Boolean = mRelativeModeEnabled
    override fun setRelativeMouseEnabled(enabled: Boolean): Boolean {
        mRelativeModeEnabled = enabled
        return true
    }

    override fun getEventX(event: MotionEvent, pointerIndex: Int): Float {
        return if (mRelativeModeEnabled && event.getToolType(pointerIndex) == MotionEvent.TOOL_TYPE_MOUSE) {
            event.getAxisValue(MotionEvent.AXIS_RELATIVE_X, pointerIndex)
        } else {
            event.getX(pointerIndex)
        }
    }

    override fun getEventY(event: MotionEvent, pointerIndex: Int): Float {
        return if (mRelativeModeEnabled && event.getToolType(pointerIndex) == MotionEvent.TOOL_TYPE_MOUSE) {
            event.getAxisValue(MotionEvent.AXIS_RELATIVE_Y, pointerIndex)
        } else {
            event.getY(pointerIndex)
        }
    }
}

class SDLGenericMotionListener_API26 : SDLGenericMotionListener_API24() {
    private var mRelativeModeEnabled = false

    override fun supportsRelativeMouse(): Boolean {
        return !SDLActivity.isDeXMode() || Build.VERSION.SDK_INT >= Build.VERSION_CODES.O_MR1
    }

    override fun inRelativeMode(): Boolean = mRelativeModeEnabled

    override fun setRelativeMouseEnabled(enabled: Boolean): Boolean {
        return if (!SDLActivity.isDeXMode() || Build.VERSION.SDK_INT >= Build.VERSION_CODES.O_MR1) {
            if (enabled) {
                SDLActivity.getContentView().requestPointerCapture()
            } else {
                SDLActivity.getContentView().releasePointerCapture()
            }
            mRelativeModeEnabled = enabled
            true
        } else {
            false
        }
    }

    override fun reclaimRelativeMouseModeIfNeeded() {
        if (mRelativeModeEnabled && !SDLActivity.isDeXMode()) {
            SDLActivity.getContentView().requestPointerCapture()
        }
    }

    override fun checkRelativeEvent(event: MotionEvent): Boolean {
        return event.source == InputDevice.SOURCE_MOUSE_RELATIVE
    }
}

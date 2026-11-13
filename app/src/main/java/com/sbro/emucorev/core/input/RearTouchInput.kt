package com.sbro.emucorev.core.input

import android.view.InputDevice
import android.view.KeyEvent
import android.view.MotionEvent

object RearTouchNative {
    external fun setContact(slot: Int, pressed: Boolean, x: Float, y: Float)
    external fun clearContacts()
    external fun setCamerasSuspended(suspended: Boolean)
}

data class RearMotionResult(val event: MotionEvent? = null, val consumed: Boolean = false)

/** Activity-scoped input dispatch. No controls are reassigned until a binding is explicitly saved. */
class RearTouchInput(
    emit: (Int, Boolean, Float, Float) -> Unit = RearTouchNative::setContact,
    private val clearNative: () -> Unit = RearTouchNative::clearContacts
) {
    val dispatcher = RearTouchDispatcher(emit)
    var capture: ((RearTouchBinding) -> Unit)? = null
    var stretchDisplay = false
    private val axisGate = RearAxisGate()
    private var active = false
    private val ownedMouseStreams = mutableSetOf<Int>()
    private var pointerX = .5f
    private var pointerY = .5f

    fun configure(profile: RearTouchProfile) {
        dispatcher.configure(profile)
        axisGate.reset()
        ownedMouseStreams.clear()
    }

    fun setActive(enabled: Boolean) {
        if (!enabled) ownedMouseStreams.clear()
        if (active == enabled) return
        active = enabled
        dispatcher.setActive(enabled)
        axisGate.setActive(enabled)
        if (!enabled) clearNative()
    }

    fun releaseAll() {
        dispatcher.reset()
        ownedMouseStreams.clear()
        axisGate.reset()
        clearNative()
    }

    fun removeDevice(id: Int) { dispatcher.releaseDevice(id); axisGate.removeDevice(id); ownedMouseStreams.remove(id) }

    fun key(event: KeyEvent): Boolean {
        val callback = capture
        if (callback != null && event.action == KeyEvent.ACTION_DOWN && event.repeatCount == 0 && !event.isCanceled) {
            if (event.keyCode in systemKeys) return false
            capture = null
            callback(RearTouchBinding(RearBindingKind.KEY, event.keyCode, device = event.device?.descriptor.orEmpty()))
            return true
        }
        if (callback != null) return event.keyCode !in systemKeys
        val matches = dispatcher.profile.points.filter { point -> point.bindings.any { b ->
            b.kind == RearBindingKind.KEY && b.code == event.keyCode && b.matches(event.device)
        } }
        if (matches.isEmpty()) return false
        if (!active) {
            if (event.action == KeyEvent.ACTION_UP) matches.forEach {
                dispatcher.setSource(it.slot, "device:${event.deviceId}:key:${event.keyCode}", false)
            }
            return false
        }
        if (event.action == KeyEvent.ACTION_UP || event.isCanceled || event.repeatCount == 0) {
            matches.forEach { dispatcher.setSource(it.slot, "device:${event.deviceId}:key:${event.keyCode}", event.action == KeyEvent.ACTION_DOWN && !event.isCanceled) }
        }
        // Consume saved bindings, including their release, before SDL/keyboard default mappings.
        return true
    }

    /** Returns a copy only when selected axes/buttons must be neutralized before forwarding to SDL. */
    fun motion(event: MotionEvent, viewWidth: Int, viewHeight: Int): RearMotionResult {
        val gamepad = event.isFromSource(InputDevice.SOURCE_JOYSTICK)
        val relativeMouse = event.isFromSource(InputDevice.SOURCE_MOUSE_RELATIVE)
        val mouse = event.isFromSource(InputDevice.SOURCE_MOUSE) || relativeMouse
        if (!gamepad && !mouse) return RearMotionResult()
        val callback = capture
        if (callback != null) {
            val binding = if (mouse && event.actionMasked in listOf(MotionEvent.ACTION_BUTTON_PRESS, MotionEvent.ACTION_DOWN)) {
                val button = event.actionButton.takeIf { it != 0 } ?: Integer.lowestOneBit(event.buttonState).takeIf { it != 0 } ?: MotionEvent.BUTTON_PRIMARY
                RearTouchBinding(RearBindingKind.MOUSE, button, device = event.device?.descriptor.orEmpty())
            } else if (gamepad) {
                event.device?.motionRanges?.filter { it.isFromSource(InputDevice.SOURCE_JOYSTICK) }
                    ?.firstOrNull { range -> kotlin.math.abs(axisValue(event,range.axis)) > .75f }
                    ?.let { range -> RearTouchBinding(RearBindingKind.AXIS, range.axis,
                        if (axisValue(event,range.axis) < 0) -1 else 1, event.device?.descriptor.orEmpty()) }
            } else null
            if (binding != null) { capture = null; callback(binding) }
            return RearMotionResult(consumed = true)
        }
        if (active) {
            if (gamepad) {
                dispatcher.move(RearPositionMode.LEFT_STICK, (event.getAxisValue(MotionEvent.AXIS_X)+1)/2, (event.getAxisValue(MotionEvent.AXIS_Y)+1)/2)
                dispatcher.move(RearPositionMode.RIGHT_STICK, (event.getAxisValue(MotionEvent.AXIS_Z)+1)/2, (event.getAxisValue(MotionEvent.AXIS_RZ)+1)/2)
            } else if (viewWidth > 0 && viewHeight > 0) {
                val position = if (relativeMouse) rearPointerPosition(viewWidth/2f + event.x, viewHeight/2f + event.y, viewWidth, viewHeight, stretchDisplay)
                    ?.let { (x,y) -> (pointerX+x-.5f).coerceIn(0f,1f) to (pointerY+y-.5f).coerceIn(0f,1f) }
                    else rearPointerPosition(event.x, event.y, viewWidth, viewHeight, stretchDisplay)
                position?.let { (x,y) ->
                    pointerX=x; pointerY=y
                    dispatcher.move(RearPositionMode.POINTER, pointerX, pointerY)
                }
            }
        }
        val blockedAxes = mutableSetOf<Int>()
        var blockedButtons = 0
        dispatcher.profile.points.forEach { p -> p.bindings.filter { it.matches(event.device) }.forEach { b ->
            val source = "device:${event.deviceId}:${b.kind}:${b.code}:${b.direction}"
            when (b.kind) {
                RearBindingKind.AXIS -> if (gamepad) {
                    val value = axisValue(event,b.code) * b.direction
                    val down = axisGate.update(source, value)
                    dispatcher.setSource(p.slot, source, down)
                    if (active) blockedAxes.add(b.code)
                }
                RearBindingKind.MOUSE -> if (mouse) {
                    dispatcher.setSource(p.slot, source, event.actionMasked != MotionEvent.ACTION_CANCEL && event.buttonState and b.code != 0)
                    if (active) blockedButtons = blockedButtons or b.code
                }
                else -> Unit
            }
        } }
        if (gamepad && active) dispatcher.profile.points.filter { dispatcher.isHeld(it.slot) }.forEach { p ->
            when (p.positionMode) {
                RearPositionMode.LEFT_STICK -> { blockedAxes.add(MotionEvent.AXIS_X); blockedAxes.add(MotionEvent.AXIS_Y) }
                RearPositionMode.RIGHT_STICK -> { blockedAxes.add(MotionEvent.AXIS_Z); blockedAxes.add(MotionEvent.AXIS_RZ) }
                else -> Unit
            }
        }
        if (mouse && active) {
            val selectedButton = event.actionButton and blockedButtons != 0
            val boundOnlyDown = event.actionMasked == MotionEvent.ACTION_DOWN && blockedButtons != 0 &&
                event.buttonState and blockedButtons != 0 && event.buttonState and blockedButtons.inv() == 0
            if (boundOnlyDown) ownedMouseStreams.add(event.deviceId)
            val ownedUp = event.actionMasked in listOf(MotionEvent.ACTION_UP, MotionEvent.ACTION_CANCEL) && ownedMouseStreams.remove(event.deviceId)
            val pointerMove = event.actionMasked == MotionEvent.ACTION_MOVE && dispatcher.profile.points.any {
                it.positionMode == RearPositionMode.POINTER && dispatcher.isHeld(it.slot)
            }
            if (boundOnlyDown || ownedUp || pointerMove || selectedButton && event.actionMasked in listOf(MotionEvent.ACTION_BUTTON_PRESS, MotionEvent.ACTION_BUTTON_RELEASE))
                return RearMotionResult(consumed = true)
        }
        if (blockedAxes.isEmpty() && blockedButtons == 0) return RearMotionResult()
        val properties = Array(event.pointerCount) { MotionEvent.PointerProperties().also { p -> event.getPointerProperties(it, p) } }
        val coords = Array(event.pointerCount) { MotionEvent.PointerCoords().also { p -> event.getPointerCoords(it, p); blockedAxes.forEach { axis -> p.setAxisValue(axis, 0f) } } }
        return RearMotionResult(MotionEvent.obtain(event.downTime, event.eventTime, event.action, event.pointerCount, properties, coords,
            event.metaState, event.buttonState and blockedButtons.inv(), event.xPrecision, event.yPrecision,
            event.deviceId, event.edgeFlags, event.source, event.flags))
    }

    private fun RearTouchBinding.matches(input: InputDevice?): Boolean = device.isEmpty() || device == input?.descriptor
    private fun axisValue(event: MotionEvent, axis: Int): Float {
        val range = event.device?.getMotionRange(axis,event.source) ?: event.device?.getMotionRange(axis)
        val trigger = axis in triggerAxes
        return rearAxisValue(event.getAxisValue(axis),range?.min,range?.max,trigger)
    }
    private val triggerAxes = setOf(MotionEvent.AXIS_LTRIGGER,MotionEvent.AXIS_RTRIGGER,MotionEvent.AXIS_BRAKE,MotionEvent.AXIS_GAS)
    private val systemKeys = setOf(KeyEvent.KEYCODE_BACK, KeyEvent.KEYCODE_HOME, KeyEvent.KEYCODE_APP_SWITCH,
        KeyEvent.KEYCODE_VOLUME_UP, KeyEvent.KEYCODE_VOLUME_DOWN, KeyEvent.KEYCODE_VOLUME_MUTE, KeyEvent.KEYCODE_POWER)
}

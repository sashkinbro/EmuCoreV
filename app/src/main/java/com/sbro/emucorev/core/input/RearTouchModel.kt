package com.sbro.emucorev.core.input

enum class RearBindingKind { KEY, AXIS, MOUSE }
enum class RearPositionMode { FIXED, SCREEN_DRAG, LEFT_STICK, RIGHT_STICK, POINTER }

data class RearTouchBinding(
    val kind: RearBindingKind,
    val code: Int,
    val direction: Int = 1,
    val device: String = ""
)

data class RearTouchPoint(
    val slot: Int,
    val x: Float = 0.5f,
    val y: Float = 0.5f,
    val screenEnabled: Boolean = false,
    val screenX: Float = if (slot % 2 == 0) 0.06f else 0.88f,
    val screenY: Float = if (slot < 2) 0.30f else 0.58f,
    val positionMode: RearPositionMode = RearPositionMode.FIXED,
    val bindings: List<RearTouchBinding> = emptyList()
) {
    fun normalized() = copy(
        x = x.safeCoordinate(), y = y.safeCoordinate(),
        screenX = screenX.safeCoordinate().coerceAtMost(0.9f),
        screenY = screenY.safeCoordinate().coerceAtMost(0.9f),
        bindings = bindings.filter { it.code >= 0 }.distinct().take(8)
    )
}

data class RearTouchProfile(val points: List<RearTouchPoint> = defaults()) {
    fun normalized() = copy(points = (0..3).map { slot ->
        points.firstOrNull { it.slot == slot }?.normalized() ?: defaults()[slot]
    })

    companion object {
        fun defaults() = listOf(
            RearTouchPoint(0, 0.25f, 0.35f), RearTouchPoint(1, 0.75f, 0.35f),
            RearTouchPoint(2, 0.25f, 0.65f), RearTouchPoint(3, 0.75f, 0.65f)
        )
    }
}

private fun Float.safeCoordinate(): Float = if (isFinite()) coerceIn(0f, 1f) else 0.5f

/** Android triggers can expose [-1,1] or [0,1]; their resting end is never a direction. */
fun rearAxisValue(value: Float, minimum: Float?, maximum: Float?, trigger: Boolean): Float {
    if (!value.isFinite()) return 0f
    if (!trigger) return value.coerceIn(-1f,1f)
    if (minimum == null || maximum == null || !minimum.isFinite() || !maximum.isFinite())
        return value.coerceIn(0f,1f)
    if (maximum <= minimum) return 0f
    return ((value-minimum)/(maximum-minimum)).coerceIn(0f,1f)
}

/** Pointer coordinates use the visible Vita viewport, excluding letterbox/pillarbox bars. */
fun rearPointerPosition(x: Float, y: Float, width: Int, height: Int, stretch: Boolean): Pair<Float, Float>? {
    if (width <= 0 || height <= 0 || !x.isFinite() || !y.isFinite()) return null
    val viewportWidth = if (stretch) width.toFloat() else minOf(width.toFloat(), height * (960f / 544f))
    val viewportHeight = if (stretch) height.toFloat() else minOf(height.toFloat(), width * (544f / 960f))
    return ((x - (width - viewportWidth) / 2) / viewportWidth).coerceIn(0f, 1f) to
        ((y - (height - viewportHeight) / 2) / viewportHeight).coerceIn(0f, 1f)
}

/** Trigger/stick-direction hysteresis with neutral rearming after pause or profile changes. */
class RearAxisGate {
    private var active = false
    private val held = mutableSetOf<String>()
    private val blocked = mutableSetOf<String>()
    fun setActive(enabled: Boolean) { if (!enabled) reset(); active = enabled }
    fun reset() { blocked.addAll(held); held.clear() }
    fun update(source: String, value: Float): Boolean {
        if (!value.isFinite()) return false
        if (value < .35f) blocked.remove(source)
        if (!active) {
            if (value > .6f) blocked.add(source)
            held.remove(source)
            return false
        }
        val down = source !in blocked && value > if (source in held) .35f else .6f
        if (down) held.add(source) else held.remove(source)
        return down
    }
    fun removeDevice(deviceId: Int) {
        val prefix = "device:$deviceId:"
        held.removeAll { it.startsWith(prefix) }; blocked.removeAll { it.startsWith(prefix) }
    }
}

/** Pure held-source merger. An UP from one input never releases another input's hold. */
class RearTouchDispatcher(private val emit: (slot: Int, pressed: Boolean, x: Float, y: Float) -> Unit) {
    var profile = RearTouchProfile()
        private set
    private var active = false
    private val sources = mutableMapOf<Int, MutableSet<String>>()
    private val blocked = mutableSetOf<String>()
    private val positions = mutableMapOf<Int, Pair<Float, Float>>()

    fun configure(next: RearTouchProfile) {
        reset()
        profile = next.normalized()
    }

    fun setActive(value: Boolean) {
        if (active == value) return
        reset()
        active = value
    }

    fun setSource(slot: Int, source: String, pressed: Boolean) {
        val point = profile.points.firstOrNull { it.slot == slot } ?: return
        if (!pressed) blocked.remove(source)
        if (pressed && (!active || source in blocked)) {
            blocked.add(source)
            return
        }
        val held = sources.getOrPut(slot) { mutableSetOf() }
        val wasPressed = held.isNotEmpty()
        if (pressed) held.add(source) else held.remove(source)
        val isPressed = held.isNotEmpty()
        if (wasPressed != isPressed) {
            val position = positions[slot] ?: (point.x to point.y)
            emit(slot, isPressed, position.first, position.second)
        }
    }

    fun move(mode: RearPositionMode, x: Float, y: Float) {
        if (!active || mode == RearPositionMode.FIXED || !x.isFinite() || !y.isFinite()) return
        profile.points.filter { it.positionMode == mode }.forEach { point ->
            val next = x.coerceIn(0f, 1f) to y.coerceIn(0f, 1f)
            if (positions.put(point.slot, next) != next && sources[point.slot]?.isNotEmpty() == true)
                emit(point.slot, true, next.first, next.second)
        }
    }

    fun movePoint(slot: Int, x: Float, y: Float) {
        if (!active || !x.isFinite() || !y.isFinite()) return
        val position = x.coerceIn(0f, 1f) to y.coerceIn(0f, 1f)
        if (positions.put(slot, position) != position && isHeld(slot))
            emit(slot, true, position.first, position.second)
    }

    fun isHeld(slot: Int): Boolean = sources[slot]?.isNotEmpty() == true

    fun releaseDevice(deviceId: Int) {
        val prefix = "device:$deviceId:"
        sources.toMap().forEach { (slot, held) ->
            held.filter { it.startsWith(prefix) }.toList().forEach { setSource(slot, it, false) }
        }
        blocked.removeAll { it.startsWith(prefix) }
    }

    fun reset() {
        sources.forEach { (slot, held) ->
            if (held.isNotEmpty()) {
                blocked.addAll(held)
                val point = profile.points.first { it.slot == slot }
                emit(slot, false, point.x, point.y)
            }
        }
        sources.clear()
        positions.clear()
    }
}

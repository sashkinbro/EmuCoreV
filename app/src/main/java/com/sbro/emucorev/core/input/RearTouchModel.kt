package com.sbro.emucorev.core.input

enum class RearBindingKind { KEY, AXIS, MOUSE }
/** ZONE: the screen pointer position inside [RearTouchProfile.zone] is mapped onto the whole rear panel. */
enum class RearPositionMode { FIXED, SCREEN_DRAG, LEFT_STICK, RIGHT_STICK, POINTER, ZONE }
/** What a press of an assigned input does to the point's contact. */
enum class RearTriggerMode { HOLD, TOGGLE, TAP }

/** Aspect of the rear panel in touch units (touch.cpp maps x to 0..1919 and y into 108..889), used by the editor pad. */
const val REAR_PANEL_WIDTH = 1919f
const val REAR_PANEL_HEIGHT = 890f

data class RearTouchBinding(
    val kind: RearBindingKind,
    val code: Int,
    val direction: Int = 1,
    val device: String = ""
)

/** Screen rectangle (fractions of the visible Vita viewport) that is stretched over the whole rear panel. */
data class RearZone(val left: Float = 0f, val top: Float = 0f, val right: Float = 1f, val bottom: Float = 1f) {
    fun normalized(): RearZone {
        val l = left.safeEdge().coerceAtMost(1f - MIN_SIZE)
        val t = top.safeEdge().coerceAtMost(1f - MIN_SIZE)
        return RearZone(l, t, right.safeEdge().coerceAtLeast(l + MIN_SIZE), bottom.safeEdge().coerceAtLeast(t + MIN_SIZE))
    }

    fun map(x: Float, y: Float): Pair<Float, Float> {
        val z = normalized()
        return ((x - z.left) / (z.right - z.left)).coerceIn(0f, 1f) to ((y - z.top) / (z.bottom - z.top)).coerceIn(0f, 1f)
    }

    private fun Float.safeEdge() = if (isFinite()) coerceIn(0f, 1f) else 0f

    companion object {
        const val MIN_SIZE = 0.1f
        val FULL = RearZone()
    }
}

enum class RearZonePreset(val zone: RearZone) {
    FULL(RearZone.FULL),
    LEFT_HALF(RearZone(0f, 0f, .5f, 1f)),
    RIGHT_HALF(RearZone(.5f, 0f, 1f, 1f)),
    TOP_HALF(RearZone(0f, 0f, 1f, .5f)),
    BOTTOM_HALF(RearZone(0f, .5f, 1f, 1f)),
    CENTER(RearZone(.25f, .25f, .75f, .75f));

    companion object {
        fun match(zone: RearZone): RearZonePreset? = entries.firstOrNull { it.zone == zone.normalized() }
    }
}

data class RearTouchPoint(
    val slot: Int,
    val x: Float = 0.5f,
    val y: Float = 0.5f,
    val screenEnabled: Boolean = false,
    val screenX: Float = if (slot % 2 == 0) 0.06f else 0.88f,
    val screenY: Float = if (slot < 2) 0.30f else 0.58f,
    val positionMode: RearPositionMode = RearPositionMode.FIXED,
    val bindings: List<RearTouchBinding> = emptyList(),
    val trigger: RearTriggerMode = RearTriggerMode.HOLD
) {
    fun normalized() = copy(
        x = x.safeCoordinate(), y = y.safeCoordinate(),
        screenX = screenX.safeCoordinate().coerceAtMost(0.9f),
        screenY = screenY.safeCoordinate().coerceAtMost(0.9f),
        bindings = bindings.filter { it.code >= 0 }.distinct().take(8)
    )
}

data class RearTouchProfile(val points: List<RearTouchPoint> = defaults(), val zone: RearZone = RearZone.FULL) {
    fun normalized() = copy(points = (0..3).map { slot ->
        points.firstOrNull { it.slot == slot }?.normalized() ?: defaults()[slot]
    }, zone = zone.normalized())

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

/** How a source (button, key, axis, mouse button, on-screen button) drives a rear point. */
const val REAR_TAP_MILLIS = 90L // provisional: a few guest frames, long enough for the title to sample the contact

/**
 * Pure source merger. HOLD: contact while any source is held. TOGGLE: a press flips the contact, releases do
 * nothing. TAP: a press makes a short contact released by [schedule]. An UP from one input never releases
 * another input's hold.
 */
class RearTouchDispatcher(
    private val emit: (slot: Int, pressed: Boolean, x: Float, y: Float) -> Unit,
    /** Runs [task] after the delay; the default runs it at once (tests/hosts without a timer). */
    private val schedule: (delayMillis: Long, task: () -> Unit) -> Unit = { _, task -> task() }
) {
    var profile = RearTouchProfile()
        private set
    /** Called after a slot's contact state changed (UI highlight of latched points). */
    var contactListener: (() -> Unit)? = null
    private var active = false
    private val sources = mutableMapOf<Int, MutableSet<String>>()
    private val contact = mutableSetOf<Int>()
    private val tapToken = mutableMapOf<Int, Int>()
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

    private fun setContact(slot: Int, down: Boolean) {
        if ((slot in contact) == down) return
        if (down) contact.add(slot) else contact.remove(slot)
        val point = profile.points.firstOrNull { it.slot == slot }
        val position = positions[slot] ?: (point?.x ?: .5f) to (point?.y ?: .5f)
        emit(slot, down, position.first, position.second)
        contactListener?.invoke()
    }

    fun setSource(slot: Int, source: String, pressed: Boolean) {
        val point = profile.points.firstOrNull { it.slot == slot } ?: return
        if (!pressed) blocked.remove(source)
        if (pressed && (!active || source in blocked)) {
            blocked.add(source)
            return
        }
        val held = sources.getOrPut(slot) { mutableSetOf() }
        val edge = if (pressed) held.add(source) else held.remove(source)
        when (point.trigger) {
            RearTriggerMode.HOLD -> setContact(slot, held.isNotEmpty())
            RearTriggerMode.TOGGLE -> if (pressed && edge) {
                tapToken.merge(slot, 1, Int::plus)
                setContact(slot, slot !in contact)
            }
            RearTriggerMode.TAP -> if (pressed && edge) {
                val token = (tapToken[slot] ?: 0) + 1
                tapToken[slot] = token
                setContact(slot, true)
                schedule(REAR_TAP_MILLIS) { if (tapToken[slot] == token) setContact(slot, false) }
            }
        }
    }

    /** Position follows [mode]; ZONE positions are mapped through the profile's rear zone. */
    fun move(mode: RearPositionMode, x: Float, y: Float) {
        if (!active || mode == RearPositionMode.FIXED || !x.isFinite() || !y.isFinite()) return
        val mapped = if (mode == RearPositionMode.ZONE) profile.zone.map(x, y) else x.coerceIn(0f, 1f) to y.coerceIn(0f, 1f)
        profile.points.filter { it.positionMode == mode }.forEach { point ->
            if (positions.put(point.slot, mapped) != mapped && point.slot in contact)
                emit(point.slot, true, mapped.first, mapped.second)
        }
    }

    fun movePoint(slot: Int, x: Float, y: Float) {
        if (!active || !x.isFinite() || !y.isFinite()) return
        val position = x.coerceIn(0f, 1f) to y.coerceIn(0f, 1f)
        if (positions.put(slot, position) != position && slot in contact)
            emit(slot, true, position.first, position.second)
    }

    /** True while the point has a contact (held, latched or inside a tap). */
    fun isHeld(slot: Int): Boolean = slot in contact

    fun releaseDevice(deviceId: Int) {
        val prefix = "device:$deviceId:"
        sources.toMap().forEach { (slot, held) ->
            held.filter { it.startsWith(prefix) }.toList().forEach { setSource(slot, it, false) }
        }
        blocked.removeAll { it.startsWith(prefix) }
    }

    fun reset() {
        sources.values.forEach { blocked.addAll(it) }
        sources.clear()
        tapToken.keys.toList().forEach { tapToken.merge(it, 1, Int::plus) } // cancels pending taps
        val down = contact.toList()
        contact.clear()
        down.forEach { slot ->
            val point = profile.points.firstOrNull { it.slot == slot }
            emit(slot, false, point?.x ?: .5f, point?.y ?: .5f)
        }
        positions.clear()
        if (down.isNotEmpty()) contactListener?.invoke()
    }
}

/** 0 left, 1 right, 2 middle, 3 back, 4 forward, -1 anything else (MotionEvent.BUTTON_* masks). */
fun rearMouseButtonIndex(mask: Int): Int = when (mask) {
    1 -> 0 // BUTTON_PRIMARY
    2 -> 1 // BUTTON_SECONDARY
    4 -> 2 // BUTTON_TERTIARY
    8 -> 3 // BUTTON_BACK
    16 -> 4 // BUTTON_FORWARD
    else -> -1
}

/** The rectangle spanned by two dragged corners (fractions of the viewport), kept at least RearZone.MIN_SIZE wide. */
fun rearZoneFromDrag(startX: Float, startY: Float, endX: Float, endY: Float): RearZone =
    RearZone(minOf(startX, endX), minOf(startY, endY), maxOf(startX, endX), maxOf(startY, endY)).normalized()

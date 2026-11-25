package com.sbro.emucorev.core.vita

import kotlin.math.abs
import kotlin.math.roundToInt

/**
 * Panel mode for a game session. The Vita refreshes at 60 Hz, so a guest frame is shown for an even
 * number of panel refreshes only when the panel runs at a multiple of 60 (60, 120, 180...). A 90 or
 * 144 Hz panel shows 60 fps content as 2,1,2,1 (or 3,2,3,2,2) refreshes: permanent judder.
 */
object GameDisplayModePolicy {
    data class Mode(val id: Int, val width: Int, val height: Int, val refreshRate: Float)

    private const val GUEST_HZ = 60f

    /** Allowed distance from an exact multiple; panels report 59.99 / 119.99 / 120.0001 style rates. */
    private const val TOLERANCE_HZ = 0.5f

    fun isMultipleOfGuestRate(refreshRate: Float): Boolean {
        val multiple = (refreshRate / GUEST_HZ).roundToInt()
        return multiple >= 1 && abs(refreshRate - multiple * GUEST_HZ) <= TOLERANCE_HZ
    }

    /**
     * The mode to request, or null to keep the current one: the current mode when it already is a
     * multiple of 60 Hz, else the lowest multiple of 60 Hz that has the current resolution (lowest
     * rate = least compositor/GPU power; the emulator renders at the guest rate anyway).
     * A panel without such a mode keeps its rate and the native side paces presentation instead.
     */
    fun pick(current: Mode, supported: List<Mode>): Mode? {
        if (isMultipleOfGuestRate(current.refreshRate)) return null
        return supported
            .filter { it.width == current.width && it.height == current.height && isMultipleOfGuestRate(it.refreshRate) }
            .minByOrNull { it.refreshRate }
    }
}

package com.sbro.emucorev.ui.emulation

import kotlin.math.roundToInt

// Adapted from EmuCoreX OverlayGridSnapping: retain sub-cell movement and snap
// absolute canvas positions so an off-grid control aligns on its first drag.
internal fun snapTouchDrag(
    residuals: MutableMap<String, Pair<Float, Float>>,
    controlId: String,
    currentX: Float,
    currentY: Float,
    delta: Pair<Float, Float>,
    stepPx: Float,
    enabled: Boolean
): Pair<Float, Float> {
    if (!enabled || !stepPx.isFinite() || stepPx <= 0f) return delta
    val residual = residuals[controlId] ?: (0f to 0f)
    val totalX = delta.first + residual.first
    val totalY = delta.second + residual.second
    val appliedX = ((currentX + totalX) / stepPx).roundToInt() * stepPx - currentX
    val appliedY = ((currentY + totalY) / stepPx).roundToInt() * stepPx - currentY
    residuals[controlId] = (totalX - appliedX).coerceIn(-stepPx, stepPx) to
        (totalY - appliedY).coerceIn(-stepPx, stepPx)
    return appliedX to appliedY
}

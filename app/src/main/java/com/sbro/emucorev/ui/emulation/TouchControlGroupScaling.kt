package com.sbro.emucorev.ui.emulation

import kotlin.math.min

internal const val GROUP_SCALE_STEP_PERCENT = 10
internal const val GROUP_SCALE_MIN_PERCENT = 50
internal const val GROUP_SCALE_MAX_PERCENT = 250

private const val MIN_ELEMENT_SIZE = 0.015f
private const val MAX_ELEMENT_SIZE = 0.5f

/**
 * Scales every control of a selected group around the group's visual centre so the whole
 * block keeps its relative layout while all buttons change size together.
 */
internal fun List<TouchControlElement>.scaleGroupAroundCenter(factor: Float): List<TouchControlElement> {
    if (size < 2 || !factor.isFinite() || factor <= 0f) return this

    val left = minOf { it.x }
    val top = minOf { it.y }
    val right = maxOf { it.x + it.width }
    val bottom = maxOf { it.y + it.height }
    val centerX = (left + right) / 2f
    val centerY = (top + bottom) / 2f

    return map { element ->
        val nextWidth = (element.width * factor).coerceIn(MIN_ELEMENT_SIZE, MAX_ELEMENT_SIZE)
        val nextHeight = (element.height * factor).coerceIn(MIN_ELEMENT_SIZE, MAX_ELEMENT_SIZE)
        val positionFactor = min(nextWidth / element.width, nextHeight / element.height)
        element.copy(
            x = (centerX + (element.x - centerX) * positionFactor)
                .coerceIn(0f, 1f - nextWidth),
            y = (centerY + (element.y - centerY) * positionFactor)
                .coerceIn(0f, 1f - nextHeight),
            width = nextWidth,
            height = nextHeight
        )
    }
}

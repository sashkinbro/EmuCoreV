package com.sbro.emucorev.ui.emulation

import kotlin.math.min

/** EmuCoreX's primary row, shoulder stacks and bottom centre row, adapted for Vita. */
internal fun buildDefaultTouchLayout(
    canvasWidth: Float, canvasHeight: Float, isLandscape: Boolean, overlayScale: Float,
    density: Float, sidePaddingPx: Float, bottomPaddingPx: Float, shoulderTopPaddingPx: Float
): List<TouchControlElement> {
    val requested = overlayScale.takeIf(Float::isFinite)?.coerceIn(0.2f, 2f) ?: 0.9f
    val dpadBase = if (isLandscape) 130f else 150f
    val actionBase = if (isLandscape) 130f else 150f
    val analogBase = if (isLandscape) 120f else 140f
    val dpadGapBase = maxOf(if (isLandscape) 32f else 34f, dpadBase / 3f + 4f)
    val dpadExtentBase = dpadBase * 2f / 3f + dpadGapBase
    val actionExtentBase = actionBase * 2f / 3.1f + if (isLandscape) 48f else 52f
    val gapBase = if (isLandscape) 24f else 16f
    val primaryBase = maxOf(dpadExtentBase, actionExtentBase, analogBase)
    val shoulderHBase = if (isLandscape) 32f else 36f
    val centerHBase = if (isLandscape) 26f else 30f
    val widthNeeded = dpadExtentBase + actionExtentBase + analogBase * (if (isLandscape) 2 else 1) + gapBase * 2 + 18f
    val heightNeeded = primaryBase + shoulderHBase * 2 + 8 + 24 + centerHBase +
        if (isLandscape) 0f else analogBase + gapBase
    val availableWidth = (canvasWidth - sidePaddingPx * 2).coerceAtLeast(1f)
    val availableHeight = (canvasHeight - bottomPaddingPx - shoulderTopPaddingPx).coerceAtLeast(1f)
    val scale = min(requested, min(availableWidth / (widthNeeded * density), availableHeight / (heightNeeded * density)))
    fun dp(value: Float) = value * density * scale
    val analog = dp(analogBase)
    val dpadButton = dp(dpadBase / 3f)
    val dpadStep = dpadButton + dp(dpadGapBase)
    val dpadExtent = dpadStep + dpadButton
    val actionButton = dp(actionBase / 3.1f)
    val actionStep = actionButton + dp(if (isLandscape) 48f else 52f)
    val actionExtent = actionStep + actionButton
    val primaryExtent = maxOf(dpadExtent, actionExtent, analog)
    val gap = dp(gapBase)
    val centerHeight = dp(centerHBase)
    val centerWidth = dp(if (isLandscape) 72f else 81.6f)
    val centerGap = dp(if (isLandscape) 10f else 12f)
    val touchSize = centerHeight
    val centerY = canvasHeight - bottomPaddingPx - centerHeight
    val primaryTop = centerY - dp(12f) - primaryExtent
    val leftX = sidePaddingPx
    val actionX = canvasWidth - sidePaddingPx - actionExtent
    val dpadY = primaryTop + (primaryExtent - dpadExtent) / 2
    val actionY = primaryTop + (primaryExtent - actionExtent) / 2
    val rightStickX = if (isLandscape) actionX - gap - analog else actionX + (actionExtent - analog) / 2
    val rightStickY = if (isLandscape) primaryTop + primaryExtent - analog else primaryTop - gap - analog
    val shoulderWidth = dp(if (isLandscape) 65f else 72f)
    val shoulderHeight = dp(shoulderHBase)
    val shoulderGap = dp(8f)
    val shoulderY = if (isLandscape) maxOf(shoulderTopPaddingPx, primaryTop - dp(12f) - shoulderHeight * 2 - shoulderGap)
        else shoulderTopPaddingPx
    val centerGroupWidth = centerWidth * 2 + touchSize + centerGap * 2
    val centerX = (canvasWidth - centerGroupWidth) / 2
    fun element(
        id: String,
        x: Float,
        y: Float,
        width: Float,
        height: Float,
        visible: Boolean = true
    ) = TouchControlElement(
        id, x / canvasWidth, y / canvasHeight, width / canvasWidth, height / canvasHeight,
        visible = visible
    ).normalized()
    return listOf(
        element(TouchControlIds.L2, leftX, shoulderY, shoulderWidth, shoulderHeight, visible = false),
        element(TouchControlIds.L1, leftX, shoulderY + shoulderHeight + shoulderGap, shoulderWidth, shoulderHeight),
        element(TouchControlIds.R2, canvasWidth - sidePaddingPx - shoulderWidth, shoulderY, shoulderWidth, shoulderHeight, visible = false),
        element(TouchControlIds.R1, canvasWidth - sidePaddingPx - shoulderWidth, shoulderY + shoulderHeight + shoulderGap, shoulderWidth, shoulderHeight),
        element(TouchControlIds.DPAD_UP, leftX + dpadStep / 2, dpadY, dpadButton, dpadButton),
        element(TouchControlIds.DPAD_DOWN, leftX + dpadStep / 2, dpadY + dpadStep, dpadButton, dpadButton),
        element(TouchControlIds.DPAD_LEFT, leftX, dpadY + dpadStep / 2, dpadButton, dpadButton),
        element(TouchControlIds.DPAD_RIGHT, leftX + dpadStep, dpadY + dpadStep / 2, dpadButton, dpadButton),
        element(TouchControlIds.LEFT_STICK, leftX + dpadExtent + gap, primaryTop + (primaryExtent - analog) / 2, analog, analog),
        element(TouchControlIds.RIGHT_STICK, rightStickX, rightStickY, analog, analog),
        element(TouchControlIds.TRIANGLE, actionX + actionStep / 2, actionY, actionButton, actionButton),
        element(TouchControlIds.CROSS, actionX + actionStep / 2, actionY + actionStep, actionButton, actionButton),
        element(TouchControlIds.SQUARE, actionX, actionY + actionStep / 2, actionButton, actionButton),
        element(TouchControlIds.CIRCLE, actionX + actionStep, actionY + actionStep / 2, actionButton, actionButton),
        element(TouchControlIds.SELECT, centerX, centerY, centerWidth, centerHeight),
        element(TouchControlIds.TOUCH, centerX + centerWidth + centerGap, centerY, touchSize, touchSize),
        element(TouchControlIds.START, centerX + centerWidth + touchSize + centerGap * 2, centerY, centerWidth, centerHeight)
    )
}

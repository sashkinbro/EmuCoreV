package com.sbro.emucorev.ui.common

import android.view.MotionEvent
import androidx.annotation.DrawableRes
import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.animation.core.Spring
import androidx.compose.animation.core.spring
import androidx.compose.animation.core.tween
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.interaction.collectIsPressedAsState
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.offset
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.shadow
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.graphics.Shape
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.input.pointer.pointerInteropFilter
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.layout.onSizeChanged
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.IntOffset
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.sbro.emucorev.R
import com.sbro.emucorev.data.TouchControlPressEffect
import com.sbro.emucorev.data.TouchControlVisualStyle
import kotlin.math.abs
import kotlin.math.roundToInt

private val OverlaySelectedStroke = Color(0xFF7CC8FF).copy(alpha = 0.88f)
private val OverlayPreviewStroke = Color.White.copy(alpha = 0.18f)

@Composable
private fun animatedPressScale(
    pressed: Boolean,
    effect: TouchControlPressEffect,
    growScale: Float,
    label: String
): Float {
    val target = if (!pressed) {
        1f
    } else {
        when (effect) {
            TouchControlPressEffect.GROW -> growScale.coerceAtLeast(1f)
            TouchControlPressEffect.SHRINK -> 0.88f
            TouchControlPressEffect.SPRING -> 1.16f
            TouchControlPressEffect.GLOW -> 1.06f
        }
    }
    return animateFloatAsState(
        targetValue = target,
        animationSpec = if (effect == TouchControlPressEffect.SPRING) {
            spring(
                dampingRatio = Spring.DampingRatioMediumBouncy,
                stiffness = Spring.StiffnessMedium
            )
        } else {
            tween(durationMillis = if (effect == TouchControlPressEffect.SHRINK) 70 else 90)
        },
        label = label
    ).value
}

enum class OverlayDpadDirection {
    Up,
    Down,
    Left,
    Right
}

@DrawableRes
fun overlayDrawableForControl(controlId: String): Int? = when (controlId) {
    "triangle" -> R.drawable.ic_controller_triangle_button
    "cross" -> R.drawable.ic_controller_cross_button
    "square" -> R.drawable.ic_controller_square_button
    "circle" -> R.drawable.ic_controller_circle_button
    "dpad_up" -> R.drawable.ic_controller_up_button
    "dpad_down" -> R.drawable.ic_controller_down_button
    "dpad_left" -> R.drawable.ic_controller_left_button
    "dpad_right" -> R.drawable.ic_controller_right_button
    "l1" -> R.drawable.ic_controller_l1_button
    "l2" -> R.drawable.ic_controller_l2_button
    "r1" -> R.drawable.ic_controller_r1_button
    "r2" -> R.drawable.ic_controller_r2_button
    "select" -> R.drawable.ic_controller_select_button
    "start" -> R.drawable.ic_controller_start_button
    "l3" -> R.drawable.ic_controller_l3_button
    "r3" -> R.drawable.ic_controller_r3_button
    "left_input_toggle" -> R.drawable.ic_controller_analog_button
    "pressure" -> R.drawable.ic_controller_pressure_modifier
    else -> null
}

@Composable
fun VectorOverlayButton(
    @DrawableRes drawableRes: Int,
    width: Dp,
    height: Dp,
    modifier: Modifier = Modifier,
    shape: Shape = RoundedCornerShape(10.dp),
    contentScale: ContentScale = ContentScale.Fit,
    alpha: Float = 1f,
    selected: Boolean = false,
    pressed: Boolean = false,
    interactive: Boolean = true,
    pressedScale: Float = 1.3f,
    visualStyle: TouchControlVisualStyle = TouchControlVisualStyle.CLASSIC,
    pressEffect: TouchControlPressEffect = TouchControlPressEffect.GROW,
    onClick: (() -> Unit)? = null,
    onPressChange: ((Boolean) -> Unit)? = null
) {
    val interactionSource = remember { MutableInteractionSource() }
    val isPressed by interactionSource.collectIsPressedAsState()
    val isActivelyPressed = pressed || (interactive && isPressed)
    val scale = animatedPressScale(
        pressed = isActivelyPressed,
        effect = pressEffect,
        growScale = pressedScale,
        label = "vector_overlay_button_scale"
    )
    val glowProgress by animateFloatAsState(
        targetValue = if (isActivelyPressed && pressEffect == TouchControlPressEffect.GLOW) 1f else 0f,
        animationSpec = tween(durationMillis = 110),
        label = "vector_overlay_button_glow"
    )

    LaunchedEffect(isPressed, onPressChange) {
        onPressChange?.invoke(interactive && isPressed)
    }

    val clickableModifier = if (interactive && (onPressChange != null || onClick != null)) {
        Modifier.clickable(
            interactionSource = interactionSource,
            indication = null
        ) {
            onClick?.invoke()
        }
    } else if (onClick != null) {
        Modifier.clickable(onClick = onClick)
    } else {
        Modifier
    }

    val isFaceButton = drawableRes == R.drawable.ic_controller_triangle_button ||
        drawableRes == R.drawable.ic_controller_cross_button ||
        drawableRes == R.drawable.ic_controller_square_button ||
        drawableRes == R.drawable.ic_controller_circle_button
    val isShoulderButton = drawableRes == R.drawable.ic_controller_l1_button ||
        drawableRes == R.drawable.ic_controller_l2_button ||
        drawableRes == R.drawable.ic_controller_r1_button ||
        drawableRes == R.drawable.ic_controller_r2_button
    val isDirectionalButton = drawableRes == R.drawable.ic_controller_up_button ||
        drawableRes == R.drawable.ic_controller_down_button ||
        drawableRes == R.drawable.ic_controller_left_button ||
        drawableRes == R.drawable.ic_controller_right_button
    val styleShape = when (visualStyle) {
        TouchControlVisualStyle.CLASSIC -> shape
        TouchControlVisualStyle.LEGACY -> if (isFaceButton) CircleShape else RoundedCornerShape(if (isShoulderButton) 16.dp else 12.dp)
        TouchControlVisualStyle.MODERN -> if (isFaceButton) RoundedCornerShape(32) else RoundedCornerShape(if (isShoulderButton) 10.dp else 8.dp)
        TouchControlVisualStyle.ARCADE -> if (isFaceButton || isDirectionalButton) CircleShape else RoundedCornerShape(if (isShoulderButton) 18.dp else 12.dp)
        TouchControlVisualStyle.MINIMAL -> if (isFaceButton || isDirectionalButton) CircleShape else RoundedCornerShape(if (isShoulderButton) 8.dp else 6.dp)
    }
    val styleBrush = when (visualStyle) {
        TouchControlVisualStyle.CLASSIC -> null
        TouchControlVisualStyle.LEGACY -> Brush.verticalGradient(
            listOf(
                Color(0xFF626A77).copy(alpha = if (isActivelyPressed) 0.78f else 0.88f),
                Color(0xFF171A21).copy(alpha = 0.94f)
            )
        )
        TouchControlVisualStyle.MODERN -> Brush.linearGradient(
            listOf(
                MaterialTheme.colorScheme.surfaceVariant.copy(alpha = if (isActivelyPressed) 0.92f else 0.76f),
                Color(0xFF090D16).copy(alpha = 0.92f)
            )
        )
        TouchControlVisualStyle.ARCADE -> Brush.radialGradient(
            listOf(
                Color(0xFFFFD166).copy(alpha = if (isActivelyPressed) 0.96f else 0.84f),
                Color(0xFFD94C78).copy(alpha = 0.92f),
                Color(0xFF281126).copy(alpha = 0.98f)
            )
        )
        TouchControlVisualStyle.MINIMAL -> Brush.verticalGradient(
            listOf(
                MaterialTheme.colorScheme.surface.copy(alpha = if (isActivelyPressed) 0.5f else 0.28f),
                MaterialTheme.colorScheme.surface.copy(alpha = 0.12f)
            )
        )
    }
    val styleBorder = when (visualStyle) {
        TouchControlVisualStyle.CLASSIC -> null
        TouchControlVisualStyle.LEGACY -> Color.White.copy(alpha = if (isActivelyPressed) 0.48f else 0.34f)
        TouchControlVisualStyle.MODERN -> MaterialTheme.colorScheme.primary.copy(alpha = if (isActivelyPressed) 0.96f else 0.72f)
        TouchControlVisualStyle.ARCADE -> Color(0xFFFFE29A).copy(alpha = if (isActivelyPressed) 1f else 0.9f)
        TouchControlVisualStyle.MINIMAL -> MaterialTheme.colorScheme.onSurface.copy(alpha = if (isActivelyPressed) 0.88f else 0.52f)
    }

    Box(
        modifier = modifier
            .size(width = width, height = height)
            .graphicsLayer(
                scaleX = scale,
                scaleY = scale,
                alpha = alpha
            )
            .clip(styleShape)
            .then(styleBrush?.let { Modifier.background(it, styleShape) } ?: Modifier)
            .then(
                styleBorder?.let {
                    Modifier.border(
                        when (visualStyle) {
                            TouchControlVisualStyle.ARCADE -> 2.dp
                            TouchControlVisualStyle.MODERN -> 1.5.dp
                            else -> 1.dp
                        },
                        it,
                        styleShape
                    )
                } ?: Modifier
            )
            .then(
                if (glowProgress > 0.01f) {
                    Modifier.border(
                        width = (1.5f + glowProgress).dp,
                        color = MaterialTheme.colorScheme.primary.copy(alpha = 0.35f + (glowProgress * 0.6f)),
                        shape = styleShape
                    )
                } else {
                    Modifier
                }
            )
            .then(
                if (selected) {
                    Modifier.border(1.5.dp, OverlaySelectedStroke, styleShape)
                } else if (alpha < 0.65f) {
                    Modifier.border(1.dp, OverlayPreviewStroke.copy(alpha = 0.35f), styleShape)
                } else {
                    Modifier
                }
            )
            .then(clickableModifier),
        contentAlignment = Alignment.Center
    ) {
        when (visualStyle) {
            TouchControlVisualStyle.LEGACY -> Box(
                modifier = Modifier
                    .fillMaxSize()
                    .padding(3.dp)
                    .border(0.75.dp, Color.White.copy(alpha = 0.13f), styleShape)
            )
            TouchControlVisualStyle.MODERN -> {
                val accent = MaterialTheme.colorScheme.primary.copy(alpha = if (isActivelyPressed) 0.95f else 0.55f)
                Canvas(modifier = Modifier.fillMaxSize()) {
                    val inset = size.minDimension * 0.14f
                    val segment = size.minDimension * 0.18f
                    val stroke = size.minDimension * 0.025f
                    drawLine(accent, Offset(inset, inset), Offset(inset + segment, inset), stroke)
                    drawLine(accent, Offset(inset, inset), Offset(inset, inset + segment), stroke)
                    drawLine(accent, Offset(size.width - inset, size.height - inset), Offset(size.width - inset - segment, size.height - inset), stroke)
                    drawLine(accent, Offset(size.width - inset, size.height - inset), Offset(size.width - inset, size.height - inset - segment), stroke)
                }
            }
            TouchControlVisualStyle.ARCADE -> Canvas(modifier = Modifier.fillMaxSize()) {
                val inset = size.minDimension * 0.12f
                drawArc(
                    color = Color.White.copy(alpha = if (isActivelyPressed) 0.42f else 0.28f),
                    startAngle = 205f,
                    sweepAngle = 130f,
                    useCenter = false,
                    topLeft = Offset(inset, inset),
                    size = androidx.compose.ui.geometry.Size(size.width - inset * 2f, size.height - inset * 2f),
                    style = Stroke(width = size.minDimension * 0.045f, cap = StrokeCap.Round)
                )
            }
            else -> Unit
        }
        if (visualStyle == TouchControlVisualStyle.CLASSIC) {
            Image(
                painter = painterResource(drawableRes),
                contentDescription = null,
                modifier = Modifier.fillMaxSize(),
                contentScale = contentScale
            )
        } else {
            AlternativeControlGlyph(
                drawableRes = drawableRes,
                visualStyle = visualStyle,
                modifier = Modifier.fillMaxSize(if (isShoulderButton) 0.7f else 0.54f)
            )
        }
    }
}

@Composable
private fun AlternativeControlGlyph(
    @DrawableRes drawableRes: Int,
    visualStyle: TouchControlVisualStyle,
    modifier: Modifier = Modifier
) {
    val baseColor = when (visualStyle) {
        TouchControlVisualStyle.LEGACY -> Color.White.copy(alpha = 0.92f)
        TouchControlVisualStyle.ARCADE -> Color.White.copy(alpha = 0.98f)
        TouchControlVisualStyle.MINIMAL -> MaterialTheme.colorScheme.onSurface.copy(alpha = 0.88f)
        else -> MaterialTheme.colorScheme.onSurface.copy(alpha = 0.94f)
    }
    val text = when (drawableRes) {
        R.drawable.ic_controller_l1_button -> "L1"
        R.drawable.ic_controller_l2_button -> "L2"
        R.drawable.ic_controller_r1_button -> "R1"
        R.drawable.ic_controller_r2_button -> "R2"
        R.drawable.ic_controller_l3_button -> "L3"
        R.drawable.ic_controller_r3_button -> "R3"
        R.drawable.ic_controller_select_button -> "SELECT"
        R.drawable.ic_controller_start_button -> "START"
        R.drawable.ic_controller_analog_button -> "ANALOG"
        R.drawable.ic_controller_pressure_modifier -> "P"
        else -> null
    }
    if (text != null) {
        Box(modifier = modifier, contentAlignment = Alignment.Center) {
            Text(
                text = text,
                color = baseColor,
                style = MaterialTheme.typography.labelMedium.copy(
                    fontSize = if (text.length <= 2) 11.sp else 7.sp,
                    lineHeight = if (text.length <= 2) 13.sp else 9.sp,
                    letterSpacing = if (text.length <= 2) 0.2.sp else 0.sp,
                    fontWeight = FontWeight.Bold
                ),
                textAlign = TextAlign.Center,
                maxLines = 1
            )
        }
        return
    }

    Canvas(modifier = modifier) {
        val strokeWidth = size.minDimension * when (visualStyle) {
            TouchControlVisualStyle.LEGACY -> 0.095f
            TouchControlVisualStyle.ARCADE -> 0.09f
            TouchControlVisualStyle.MINIMAL -> 0.055f
            else -> 0.075f
        }
        val inset = size.minDimension * 0.17f
        val center = Offset(size.width / 2f, size.height / 2f)
        val faceColor = when (drawableRes) {
            R.drawable.ic_controller_triangle_button -> Color(0xFF63D59A)
            R.drawable.ic_controller_circle_button -> Color(0xFFFF6C82)
            R.drawable.ic_controller_cross_button -> Color(0xFF78A6FF)
            R.drawable.ic_controller_square_button -> Color(0xFFE58BEF)
            else -> baseColor
        }
        when (drawableRes) {
            R.drawable.ic_controller_triangle_button -> {
                val path = Path().apply {
                    moveTo(center.x, inset)
                    lineTo(size.width - inset, size.height - inset)
                    lineTo(inset, size.height - inset)
                    close()
                }
                drawPath(path, faceColor, style = Stroke(strokeWidth, cap = StrokeCap.Round))
            }
            R.drawable.ic_controller_circle_button -> {
                drawCircle(faceColor, size.minDimension * 0.31f, center, style = Stroke(strokeWidth))
            }
            R.drawable.ic_controller_cross_button -> {
                drawLine(faceColor, Offset(inset, inset), Offset(size.width - inset, size.height - inset), strokeWidth, StrokeCap.Round)
                drawLine(faceColor, Offset(size.width - inset, inset), Offset(inset, size.height - inset), strokeWidth, StrokeCap.Round)
            }
            R.drawable.ic_controller_square_button -> {
                drawRect(
                    faceColor,
                    topLeft = Offset(inset, inset),
                    size = androidx.compose.ui.geometry.Size(size.width - inset * 2f, size.height - inset * 2f),
                    style = Stroke(strokeWidth)
                )
            }
            R.drawable.ic_controller_up_button,
            R.drawable.ic_controller_down_button,
            R.drawable.ic_controller_left_button,
            R.drawable.ic_controller_right_button -> {
                val arrow = Path()
                when (drawableRes) {
                    R.drawable.ic_controller_up_button -> {
                        arrow.moveTo(center.x, inset)
                        arrow.lineTo(size.width - inset, size.height - inset)
                        arrow.lineTo(inset, size.height - inset)
                    }
                    R.drawable.ic_controller_down_button -> {
                        arrow.moveTo(inset, inset)
                        arrow.lineTo(size.width - inset, inset)
                        arrow.lineTo(center.x, size.height - inset)
                    }
                    R.drawable.ic_controller_left_button -> {
                        arrow.moveTo(inset, center.y)
                        arrow.lineTo(size.width - inset, inset)
                        arrow.lineTo(size.width - inset, size.height - inset)
                    }
                    else -> {
                        arrow.moveTo(inset, inset)
                        arrow.lineTo(size.width - inset, center.y)
                        arrow.lineTo(inset, size.height - inset)
                    }
                }
                arrow.close()
                drawPath(arrow, baseColor.copy(alpha = 0.88f))
            }
        }
    }
}

@Composable
fun VectorDpadCluster(
    size: Dp,
    modifier: Modifier = Modifier,
    alpha: Float = 1f,
    selected: Boolean = false,
    interactive: Boolean = true,
    visualStyle: TouchControlVisualStyle = TouchControlVisualStyle.CLASSIC,
    pressEffect: TouchControlPressEffect = TouchControlPressEffect.GROW,
    onDirectionsChange: ((Set<OverlayDpadDirection>) -> Unit)? = null
) {
    var bounds by remember { mutableStateOf(androidx.compose.ui.geometry.Size.Zero) }
    var activePointerId by remember { mutableIntStateOf(MotionEvent.INVALID_POINTER_ID) }
    var activeDirections by remember { mutableStateOf(emptySet<OverlayDpadDirection>()) }
    val currentDirections by rememberUpdatedState(activeDirections)
    val currentOnDirectionsChange by rememberUpdatedState(onDirectionsChange)

    fun setDirections(directions: Set<OverlayDpadDirection>) {
        if (directions == activeDirections) return
        activeDirections = directions
        onDirectionsChange?.invoke(directions)
    }

    fun directionsFromPosition(x: Float, y: Float): Set<OverlayDpadDirection> {
        if (bounds.width <= 0f || bounds.height <= 0f) return emptySet()
        val centerX = bounds.width / 2f
        val centerY = bounds.height / 2f
        val deadZoneX = bounds.width * 0.16f
        val deadZoneY = bounds.height * 0.16f
        val dx = x - centerX
        val dy = y - centerY
        return buildSet {
            if (dx < -deadZoneX) add(OverlayDpadDirection.Left)
            if (dx > deadZoneX) add(OverlayDpadDirection.Right)
            if (dy < -deadZoneY) add(OverlayDpadDirection.Up)
            if (dy > deadZoneY) add(OverlayDpadDirection.Down)
        }
    }

    fun resetDirections() {
        activePointerId = MotionEvent.INVALID_POINTER_ID
        setDirections(emptySet())
    }

    val pointerModifier = if (interactive && onDirectionsChange != null) {
        Modifier.pointerInteropFilter { event ->
            when (event.actionMasked) {
                MotionEvent.ACTION_DOWN, MotionEvent.ACTION_POINTER_DOWN -> {
                    if (activePointerId == MotionEvent.INVALID_POINTER_ID) {
                        val index = event.actionIndex
                        activePointerId = event.getPointerId(index)
                        setDirections(directionsFromPosition(event.getX(index), event.getY(index)))
                    }
                    true
                }

                MotionEvent.ACTION_MOVE -> {
                    val index = event.findPointerIndex(activePointerId)
                    if (index >= 0) {
                        setDirections(directionsFromPosition(event.getX(index), event.getY(index)))
                        true
                    } else {
                        false
                    }
                }

                MotionEvent.ACTION_UP, MotionEvent.ACTION_POINTER_UP -> {
                    val pointerId = event.getPointerId(event.actionIndex)
                    if (pointerId == activePointerId) {
                        resetDirections()
                    }
                    true
                }

                MotionEvent.ACTION_CANCEL -> {
                    resetDirections()
                    true
                }

                else -> activePointerId != MotionEvent.INVALID_POINTER_ID
            }
        }
    } else {
        Modifier
    }

    DisposableEffect(Unit) {
        onDispose {
            if (currentDirections.isNotEmpty()) {
                currentOnDirectionsChange?.invoke(emptySet())
            }
        }
    }

    val shape = when (visualStyle) {
        TouchControlVisualStyle.CLASSIC -> RoundedCornerShape(22.dp)
        TouchControlVisualStyle.LEGACY -> RoundedCornerShape(28.dp)
        TouchControlVisualStyle.MODERN -> RoundedCornerShape(16.dp)
        TouchControlVisualStyle.ARCADE -> CircleShape
        TouchControlVisualStyle.MINIMAL -> RoundedCornerShape(24.dp)
    }
    val buttonSize = size * 0.36f
    Box(
        modifier = modifier
            .size(size)
            .graphicsLayer(alpha = alpha)
            .then(
                if (selected) {
                    Modifier.border(1.5.dp, OverlaySelectedStroke, shape)
                } else {
                    Modifier
                }
            )
            .onSizeChanged { bounds = androidx.compose.ui.geometry.Size(it.width.toFloat(), it.height.toFloat()) }
            .then(pointerModifier),
        contentAlignment = Alignment.Center
    ) {
        VectorOverlayButton(
            drawableRes = R.drawable.ic_controller_up_button,
            width = buttonSize,
            height = buttonSize,
            shape = RoundedCornerShape(8.dp),
            pressed = activeDirections.contains(OverlayDpadDirection.Up),
            interactive = false,
            visualStyle = visualStyle,
            pressEffect = pressEffect,
            modifier = Modifier.align(Alignment.TopCenter)
        )
        VectorOverlayButton(
            drawableRes = R.drawable.ic_controller_down_button,
            width = buttonSize,
            height = buttonSize,
            shape = RoundedCornerShape(8.dp),
            pressed = activeDirections.contains(OverlayDpadDirection.Down),
            interactive = false,
            visualStyle = visualStyle,
            pressEffect = pressEffect,
            modifier = Modifier.align(Alignment.BottomCenter)
        )
        VectorOverlayButton(
            drawableRes = R.drawable.ic_controller_left_button,
            width = buttonSize,
            height = buttonSize,
            shape = RoundedCornerShape(8.dp),
            pressed = activeDirections.contains(OverlayDpadDirection.Left),
            interactive = false,
            visualStyle = visualStyle,
            pressEffect = pressEffect,
            modifier = Modifier.align(Alignment.CenterStart)
        )
        VectorOverlayButton(
            drawableRes = R.drawable.ic_controller_right_button,
            width = buttonSize,
            height = buttonSize,
            shape = RoundedCornerShape(8.dp),
            pressed = activeDirections.contains(OverlayDpadDirection.Right),
            interactive = false,
            visualStyle = visualStyle,
            pressEffect = pressEffect,
            modifier = Modifier.align(Alignment.CenterEnd)
        )
    }
}

@Composable
fun VectorAnalogStick(
    analogSize: Dp,
    modifier: Modifier = Modifier,
    analogWidth: Dp = analogSize,
    analogHeight: Dp = analogSize,
    alpha: Float = 1f,
    selected: Boolean = false,
    surfaceOnly: Boolean = false,
    visualX: Float = 0f,
    visualY: Float = 0f,
    interactive: Boolean = true,
    visualStyle: TouchControlVisualStyle = TouchControlVisualStyle.CLASSIC,
    pressEffect: TouchControlPressEffect = TouchControlPressEffect.GROW,
    pressed: Boolean = false,
    onClick: (() -> Unit)? = null,
    onTouchStart: (() -> Unit)? = null,
    onValueChange: ((Float, Float) -> Unit)? = null
) {
    var size by remember { mutableStateOf(androidx.compose.ui.geometry.Size.Zero) }
    var thumbOffset by remember { mutableStateOf(Offset.Zero) }
    var lastSentX by remember { mutableIntStateOf(0) }
    var lastSentY by remember { mutableIntStateOf(0) }
    var activePointerId by remember { mutableStateOf<Long?>(null) }

    fun dispatchStickValue(x: Float, y: Float) {
        val quantizedX = (x * 255f).roundToInt()
        val quantizedY = (y * 255f).roundToInt()
        if (quantizedX == lastSentX && quantizedY == lastSentY) return
        lastSentX = quantizedX
        lastSentY = quantizedY
        onValueChange?.invoke(quantizedX / 255f, quantizedY / 255f)
    }

    fun updateStickFromPosition(position: Offset) {
        if (size.width == 0f || size.height == 0f) return
        val center = Offset(size.width / 2f, size.height / 2f)
        val maxDistance = minOf(size.width, size.height) * 0.5f
        if (maxDistance <= 0f) return

        val deadZone = 0.12f
        val raw = position - center
        val distance = raw.getDistance()
        val clamped = if (distance > maxDistance) {
            raw * (maxDistance / distance)
        } else {
            raw
        }
        thumbOffset = clamped
        val nx = (clamped.x / maxDistance).coerceIn(-1f, 1f).let { if (abs(it) < deadZone) 0f else it }
        val ny = (clamped.y / maxDistance).coerceIn(-1f, 1f).let { if (abs(it) < deadZone) 0f else it }
        dispatchStickValue(nx, ny)
    }

    fun resetStick() {
        thumbOffset = Offset.Zero
        dispatchStickValue(0f, 0f)
    }

    val pointerModifier = if (interactive && onValueChange != null) {
        Modifier.pointerInput(Unit) {
            try {
                awaitPointerEventScope {
                    while (true) {
                        var pointerId: Long? = null

                        while (pointerId == null) {
                            val event = awaitPointerEvent()
                            val down = event.changes.firstOrNull { change ->
                                change.pressed &&
                                    !change.previousPressed &&
                                    !change.isConsumed &&
                                    change.position.x in 0f..size.width &&
                                    change.position.y in 0f..size.height
                            }
                            if (down != null) {
                                pointerId = down.id.value
                                activePointerId = pointerId
                                onTouchStart?.invoke()
                                updateStickFromPosition(down.position)
                                down.consume()
                            }
                        }

                        while (pointerId != null) {
                            val event = awaitPointerEvent()
                            val change = event.changes.firstOrNull { it.id.value == pointerId }
                                ?: continue
                            if (!change.pressed) {
                                pointerId = null
                                activePointerId = null
                                resetStick()
                                continue
                            }

                            updateStickFromPosition(change.position)
                            change.consume()
                        }
                    }
                }
            } finally {
                activePointerId = null
                resetStick()
            }
        }
    } else {
        Modifier
    }

    DisposableEffect(Unit) {
        onDispose {
            resetStick()
        }
    }

    val visualMaxDistance = minOf(size.width, size.height) * 0.5f
    val visualThumbOffset = if (visualMaxDistance > 0f) {
        Offset(
            x = visualX.coerceIn(-1f, 1f) * visualMaxDistance,
            y = visualY.coerceIn(-1f, 1f) * visualMaxDistance
        )
    } else {
        Offset.Zero
    }
    val animatedVisualThumbX by animateFloatAsState(
        targetValue = visualThumbOffset.x,
        animationSpec = tween(durationMillis = 90),
        label = "vector_analog_stick_visual_x"
    )
    val animatedVisualThumbY by animateFloatAsState(
        targetValue = visualThumbOffset.y,
        animationSpec = tween(durationMillis = 90),
        label = "vector_analog_stick_visual_y"
    )
    val displayedThumbOffset = when {
        activePointerId != null -> thumbOffset
        !interactive -> visualThumbOffset
        else -> Offset(animatedVisualThumbX, animatedVisualThumbY)
    }
    val selectionShape = if (!surfaceOnly) {
        CircleShape
    } else {
        when (visualStyle) {
            TouchControlVisualStyle.CLASSIC -> RoundedCornerShape(22.dp)
            TouchControlVisualStyle.LEGACY -> RoundedCornerShape(30.dp)
            TouchControlVisualStyle.MODERN -> RoundedCornerShape(14.dp)
            TouchControlVisualStyle.ARCADE -> RoundedCornerShape(32.dp)
            TouchControlVisualStyle.MINIMAL -> RoundedCornerShape(20.dp)
        }
    }
    val isActivelyPressed = pressed || activePointerId != null
    val pressScale = animatedPressScale(
        pressed = isActivelyPressed,
        effect = pressEffect,
        growScale = 1.12f,
        label = "vector_analog_stick_scale"
    )
    val glowProgress by animateFloatAsState(
        targetValue = if (isActivelyPressed && pressEffect == TouchControlPressEffect.GLOW) 1f else 0f,
        animationSpec = tween(durationMillis = 110),
        label = "vector_analog_stick_glow"
    )

    Box(
        modifier = modifier
            .size(width = analogWidth, height = analogHeight)
            .graphicsLayer(alpha = alpha, scaleX = pressScale, scaleY = pressScale)
            .then(
                if (glowProgress > 0.01f) {
                    Modifier.border(
                        width = (1.5f + glowProgress).dp,
                        color = MaterialTheme.colorScheme.primary.copy(alpha = 0.35f + (glowProgress * 0.6f)),
                        shape = selectionShape
                    )
                } else {
                    Modifier
                }
            )
            .then(
                if (selected) {
                    Modifier.border(
                        width = 1.5.dp,
                        color = OverlaySelectedStroke,
                        shape = selectionShape
                    )
                } else if (alpha < 0.65f) {
                    Modifier.border(
                        width = 1.dp,
                        color = OverlayPreviewStroke.copy(alpha = 0.35f),
                        shape = selectionShape
                    )
                } else {
                    Modifier
                }
            )
            .then(
                if (onClick != null) Modifier.clickable(onClick = onClick) else Modifier
            )
            .onSizeChanged { size = androidx.compose.ui.geometry.Size(it.width.toFloat(), it.height.toFloat()) }
            .then(pointerModifier),
        contentAlignment = Alignment.Center
    ) {
        if (surfaceOnly) {
            val panelShape = when (visualStyle) {
                TouchControlVisualStyle.CLASSIC -> RoundedCornerShape(22.dp)
                TouchControlVisualStyle.LEGACY -> RoundedCornerShape(30.dp)
                TouchControlVisualStyle.MODERN -> RoundedCornerShape(14.dp)
                TouchControlVisualStyle.ARCADE -> RoundedCornerShape(32.dp)
                TouchControlVisualStyle.MINIMAL -> RoundedCornerShape(20.dp)
            }
            val guideSize = minOf(analogWidth, analogHeight)
            val guideColor = when (visualStyle) {
                TouchControlVisualStyle.CLASSIC -> Color(0xFF7CC8FF).copy(alpha = 0.28f)
                TouchControlVisualStyle.LEGACY -> Color.White.copy(alpha = 0.22f)
                TouchControlVisualStyle.MODERN -> MaterialTheme.colorScheme.primary.copy(alpha = 0.44f)
                TouchControlVisualStyle.ARCADE -> Color(0xFFFFD166).copy(alpha = 0.72f)
                TouchControlVisualStyle.MINIMAL -> MaterialTheme.colorScheme.onSurface.copy(alpha = 0.38f)
            }
            val panelBrush = when (visualStyle) {
                TouchControlVisualStyle.CLASSIC -> Brush.verticalGradient(
                    listOf(Color(0xFF14213A).copy(alpha = 0.58f), Color(0xFF14213A).copy(alpha = 0.58f))
                )
                TouchControlVisualStyle.LEGACY -> Brush.verticalGradient(
                    listOf(Color(0xFF565E6A).copy(alpha = 0.72f), Color(0xFF12151B).copy(alpha = 0.9f))
                )
                TouchControlVisualStyle.MODERN -> Brush.linearGradient(
                    listOf(
                        MaterialTheme.colorScheme.primary.copy(alpha = 0.16f),
                        MaterialTheme.colorScheme.surface.copy(alpha = 0.78f)
                    )
                )
                TouchControlVisualStyle.ARCADE -> Brush.radialGradient(
                    listOf(Color(0xFF8D2F62).copy(alpha = 0.72f), Color(0xFF1D1021).copy(alpha = 0.94f))
                )
                TouchControlVisualStyle.MINIMAL -> Brush.verticalGradient(
                    listOf(
                        MaterialTheme.colorScheme.surface.copy(alpha = 0.18f),
                        MaterialTheme.colorScheme.surface.copy(alpha = 0.06f)
                    )
                )
            }
            Box(
                modifier = Modifier
                    .fillMaxSize()
                    .clip(panelShape)
                    .background(panelBrush)
                    .border(
                        when (visualStyle) {
                            TouchControlVisualStyle.ARCADE -> 2.dp
                            TouchControlVisualStyle.MODERN -> 1.5.dp
                            else -> 1.dp
                        },
                        when (visualStyle) {
                            TouchControlVisualStyle.CLASSIC -> Color.White.copy(alpha = 0.16f)
                            TouchControlVisualStyle.LEGACY -> Color.White.copy(alpha = 0.3f)
                            TouchControlVisualStyle.MODERN -> MaterialTheme.colorScheme.primary.copy(alpha = 0.58f)
                            TouchControlVisualStyle.ARCADE -> Color(0xFFFFD166).copy(alpha = 0.84f)
                            TouchControlVisualStyle.MINIMAL -> MaterialTheme.colorScheme.onSurface.copy(alpha = 0.4f)
                        },
                        panelShape
                    )
            )
            Box(
                modifier = Modifier
                    .size(width = analogWidth * 0.58f, height = 1.5.dp)
                    .background(guideColor)
            )
            Box(
                modifier = Modifier
                    .size(width = 1.5.dp, height = analogHeight * 0.54f)
                    .background(guideColor)
            )
            Box(
                modifier = Modifier
                    .size(guideSize * 0.14f)
                    .clip(
                        when (visualStyle) {
                            TouchControlVisualStyle.MODERN -> RoundedCornerShape(5.dp)
                            else -> CircleShape
                        }
                    )
                    .background(guideColor.copy(alpha = 0.78f))
                    .offset { IntOffset(displayedThumbOffset.x.roundToInt(), displayedThumbOffset.y.roundToInt()) }
            )
        } else {
            when (visualStyle) {
                TouchControlVisualStyle.CLASSIC -> {
                    Image(
                        painter = painterResource(R.drawable.ic_controller_analog_base),
                        contentDescription = null,
                        modifier = Modifier.fillMaxSize(),
                        contentScale = ContentScale.Fit
                    )
                    Image(
                        painter = painterResource(R.drawable.ic_controller_analog_stick),
                        contentDescription = null,
                        modifier = Modifier
                            .size(analogSize * 0.52f)
                            .offset { IntOffset(displayedThumbOffset.x.roundToInt(), displayedThumbOffset.y.roundToInt()) },
                        contentScale = ContentScale.Fit
                    )
                }

                TouchControlVisualStyle.LEGACY -> {
                    Box(
                        modifier = Modifier
                            .fillMaxSize(0.96f)
                            .shadow(7.dp, CircleShape, clip = false)
                            .clip(CircleShape)
                            .background(
                                Brush.radialGradient(
                                    listOf(Color(0xFF626A76), Color(0xFF171A20), Color(0xFF080A0E))
                                )
                            )
                            .border(1.5.dp, Color.White.copy(alpha = 0.34f), CircleShape)
                    )
                    Box(
                        modifier = Modifier
                            .fillMaxSize(0.72f)
                            .clip(CircleShape)
                            .border(1.dp, Color.Black.copy(alpha = 0.72f), CircleShape)
                    )
                    Box(
                        modifier = Modifier
                            .size(analogSize * 0.52f)
                            .offset { IntOffset(displayedThumbOffset.x.roundToInt(), displayedThumbOffset.y.roundToInt()) }
                            .shadow(5.dp, CircleShape, clip = false)
                            .clip(CircleShape)
                            .background(Brush.verticalGradient(listOf(Color(0xFF707885), Color(0xFF242831))))
                            .border(1.25.dp, Color.White.copy(alpha = 0.38f), CircleShape),
                        contentAlignment = Alignment.Center
                    ) {
                        Canvas(modifier = Modifier.fillMaxSize(0.62f)) {
                            val dotRadius = size.minDimension * 0.055f
                            val color = Color.Black.copy(alpha = 0.42f)
                            listOf(0.28f, 0.5f, 0.72f).forEach { x ->
                                listOf(0.28f, 0.5f, 0.72f).forEach { y ->
                                    drawCircle(color, dotRadius, Offset(size.width * x, size.height * y))
                                }
                            }
                        }
                    }
                }

                TouchControlVisualStyle.MODERN -> {
                    val accent = MaterialTheme.colorScheme.primary
                    Box(
                        modifier = Modifier
                            .fillMaxSize(0.94f)
                            .clip(CircleShape)
                            .background(
                                Brush.linearGradient(
                                    listOf(
                                        MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.9f),
                                        Color(0xFF070B12).copy(alpha = 0.96f)
                                    )
                                )
                            )
                            .border(2.dp, accent.copy(alpha = 0.68f), CircleShape)
                            .padding(4.dp)
                            .border(1.dp, accent.copy(alpha = 0.22f), CircleShape)
                    )
                    Canvas(modifier = Modifier.fillMaxSize()) {
                        val center = Offset(size.width / 2f, size.height / 2f)
                        val radius = size.minDimension / 2f
                        val outerTickRadius = radius * 0.90f
                        val innerTickRadius = radius * 0.70f
                        val stroke = size.minDimension * 0.018f
                        val color = accent.copy(alpha = 0.82f)
                        listOf(
                            Offset(0f, -1f),
                            Offset(1f, 0f),
                            Offset(0f, 1f),
                            Offset(-1f, 0f)
                        ).forEach { direction ->
                            drawLine(
                                color = color,
                                start = center + direction * outerTickRadius,
                                end = center + direction * innerTickRadius,
                                strokeWidth = stroke,
                                cap = StrokeCap.Round
                            )
                        }
                    }
                    val thumbShape = RoundedCornerShape(34)
                    Box(
                        modifier = Modifier
                            .size(analogSize * 0.48f)
                            .offset { IntOffset(displayedThumbOffset.x.roundToInt(), displayedThumbOffset.y.roundToInt()) }
                            .shadow(6.dp, thumbShape, clip = false)
                            .clip(thumbShape)
                            .background(
                                Brush.linearGradient(
                                    listOf(accent.copy(alpha = 0.72f), MaterialTheme.colorScheme.surface.copy(alpha = 0.96f))
                                )
                            )
                            .border(1.5.dp, accent.copy(alpha = 0.94f), thumbShape)
                            .padding(3.dp)
                            .border(1.dp, MaterialTheme.colorScheme.onSurface.copy(alpha = 0.26f), thumbShape)
                    )
                }

                TouchControlVisualStyle.ARCADE -> {
                    val gold = Color(0xFFFFD166)
                    Box(
                        modifier = Modifier
                            .fillMaxSize(0.96f)
                            .shadow(8.dp, CircleShape, clip = false)
                            .clip(CircleShape)
                            .background(
                                Brush.radialGradient(
                                    listOf(Color(0xFFB63D70), Color(0xFF4A193A), Color(0xFF170B18))
                                )
                            )
                            .border(2.5.dp, gold.copy(alpha = 0.92f), CircleShape)
                            .padding(5.dp)
                            .border(1.dp, Color.White.copy(alpha = 0.2f), CircleShape)
                    )
                    Canvas(modifier = Modifier.fillMaxSize(0.76f)) {
                        drawCircle(
                            color = gold.copy(alpha = 0.28f),
                            radius = size.minDimension * 0.47f,
                            style = Stroke(width = size.minDimension * 0.035f)
                        )
                        drawCircle(
                            color = Color.Black.copy(alpha = 0.42f),
                            radius = size.minDimension * 0.33f,
                            style = Stroke(width = size.minDimension * 0.025f)
                        )
                    }
                    Box(
                        modifier = Modifier
                            .size(analogSize * 0.5f)
                            .offset { IntOffset(displayedThumbOffset.x.roundToInt(), displayedThumbOffset.y.roundToInt()) }
                            .shadow(7.dp, CircleShape, clip = false)
                            .clip(CircleShape)
                            .background(
                                Brush.radialGradient(
                                    listOf(Color(0xFFFFE6A8), gold, Color(0xFFD94C78))
                                )
                            )
                            .border(2.dp, Color.White.copy(alpha = 0.66f), CircleShape),
                        contentAlignment = Alignment.Center
                    ) {
                        Box(
                            modifier = Modifier
                                .fillMaxSize(0.68f)
                                .clip(CircleShape)
                                .border(1.dp, Color(0xFF7A254E).copy(alpha = 0.78f), CircleShape)
                        )
                    }
                }

                TouchControlVisualStyle.MINIMAL -> {
                    val lineColor = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.54f)
                    Box(
                        modifier = Modifier
                            .fillMaxSize(0.9f)
                            .clip(CircleShape)
                            .background(MaterialTheme.colorScheme.surface.copy(alpha = 0.08f))
                            .border(1.5.dp, lineColor, CircleShape)
                            .padding(6.dp)
                            .border(1.dp, lineColor.copy(alpha = 0.28f), CircleShape)
                    )
                    Box(
                        modifier = Modifier
                            .size(analogSize * 0.38f)
                            .offset { IntOffset(displayedThumbOffset.x.roundToInt(), displayedThumbOffset.y.roundToInt()) }
                            .clip(CircleShape)
                            .background(MaterialTheme.colorScheme.surface.copy(alpha = 0.52f))
                            .border(1.5.dp, MaterialTheme.colorScheme.onSurface.copy(alpha = 0.72f), CircleShape)
                            .padding(4.dp)
                            .border(1.dp, lineColor.copy(alpha = 0.24f), CircleShape)
                    )
                }
            }
        }
    }
}

package com.sbro.emucorev.ui.theme.style

import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.background
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.interaction.collectIsPressedAsState
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.RowScope
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.MaterialTheme
import androidx.compose.ui.draw.drawBehind
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.RectangleShape
import androidx.compose.ui.graphics.SolidColor
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.graphics.drawOutline
import androidx.compose.ui.graphics.drawscope.translate
import com.sbro.emucorev.data.theme.ButtonVariant
import com.sbro.emucorev.data.theme.ThemeShapeStyle
import com.sbro.emucorev.ui.theme.neon.neonButtonShape
import androidx.compose.material3.ButtonColors
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.ButtonElevation
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.drawWithContent
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Shape
import androidx.compose.ui.graphics.lerp
import androidx.compose.ui.unit.dp
import com.sbro.emucorev.data.theme.UiStyle
import com.sbro.emucorev.ui.theme.LocalThemeExtras
import androidx.compose.material3.Button as M3Button
import androidx.compose.material3.FilledTonalButton as M3FilledTonalButton
import androidx.compose.material3.OutlinedButton as M3OutlinedButton
import androidx.compose.material3.TextButton as M3TextButton

/*
 * Drop-in replacements for material3 buttons that apply the theme's UI style. Under STANDARD/NEON
 * they forward unchanged. Containers are painted by the style (bevel, gloss, gradient, glass) and the
 * Material container is made transparent so the painting shows through.
 */

private class ButtonLook(val modifier: Modifier, val colors: ButtonColors, val border: BorderStroke?, val flat: Boolean)

@Composable
private fun buttonLook(
    style: UiStyle,
    base: ButtonColors,
    enabled: Boolean,
    shape: Shape,
    border: BorderStroke?,
    interactionSource: MutableInteractionSource,
    filled: Boolean,
    text: Boolean = false
): ButtonLook? {
    val pressed by interactionSource.collectIsPressedAsState()
    val variant = LocalThemeExtras.current.button
    if (variant != ButtonVariant.DEFAULT &&
        (!text || variant == ButtonVariant.UNDERLINE || variant == ButtonVariant.BRACKET)
    ) {
        return variantButtonLook(variant, base, enabled, shape)
    }
    val container = if (enabled) base.containerColor else base.disabledContainerColor
    // Text/outlined buttons have a transparent container; styles that draw raised buttons give them one.
    val fill = if (container.alpha < 0.05f) androidx.compose.material3.MaterialTheme.colorScheme.surfaceVariant else container
    fun painted(brush: Brush, extra: Modifier = Modifier, newBorder: BorderStroke? = border) = ButtonLook(
        modifier = Modifier.background(brush, shape).then(extra),
        colors = base.copy(containerColor = Color.Transparent, disabledContainerColor = Color.Transparent,
            contentColor = if (container.alpha < 0.05f) base.contentColor else base.contentColor),
        border = newBorder,
        flat = true
    )
    return when (style) {
        UiStyle.STANDARD, UiStyle.NEON, UiStyle.METRO, UiStyle.OSD, UiStyle.PAPER -> null
        UiStyle.BUBBLE -> painted(
            Brush.verticalGradient(
                0f to lerp(fill, Color.White, if (pressed) 0.1f else 0.55f),
                0.5f to lerp(fill, Color.White, 0.12f),
                1f to fill
            ),
            newBorder = BorderStroke(2.dp, lerp(fill, Color.Black, 0.18f))
        )
        UiStyle.CLASSIC -> painted(
            Brush.verticalGradient(listOf(fill, fill)),
            Modifier.overlayInShape(shape) {
                drawBevel(Color.White, Color(0xFF404040), Color(0xFF808080), sunken = pressed)
            },
            newBorder = null
        )
        UiStyle.LUNA -> painted(
            Brush.verticalGradient(
                0f to lerp(fill, Color.White, if (pressed) 0.05f else 0.45f),
                0.48f to lerp(fill, Color.White, 0.10f),
                0.52f to fill,
                1f to lerp(fill, Color.Black, 0.12f)
            ),
            newBorder = BorderStroke(1.dp, lerp(fill, Color.Black, 0.35f))
        )
        UiStyle.S60 -> painted(
            Brush.verticalGradient(listOf(lerp(fill, Color.White, if (pressed) 0f else 0.28f), lerp(fill, Color.Black, 0.10f))),
            newBorder = BorderStroke(1.dp, lerp(fill, Color.Black, 0.25f))
        )
        UiStyle.AERO -> painted(
            Brush.verticalGradient(
                0f to fill.copy(alpha = 0.55f).compositeWhite(0.35f),
                0.5f to fill.copy(alpha = 0.55f),
                1f to fill.copy(alpha = 0.75f)
            ),
            newBorder = BorderStroke(1.dp, Color.White.copy(alpha = 0.45f))
        )
        UiStyle.PIXEL -> painted(
            Brush.verticalGradient(listOf(fill, fill)),
            Modifier.overlayInShape(shape) {
                drawPixelFrame(if (pressed) base.contentColor else lerp(fill, Color.Black, 0.5f))
            },
            newBorder = null
        )
        UiStyle.TERMINAL -> if (filled) null else ButtonLook(Modifier, base, BorderStroke(1.dp, base.contentColor.copy(alpha = 0.7f)), false)
    }
}

/** Structural `components.button` variants; they replace the UI style's button painting. */
@Composable
private fun variantButtonLook(variant: ButtonVariant, base: ButtonColors, enabled: Boolean, shape: Shape): ButtonLook {
    val scheme = MaterialTheme.colorScheme
    val widthDp = LocalThemeExtras.current.borderWidthDp
    val container = if (enabled) base.containerColor else base.disabledContainerColor
    val solid = container.alpha >= 0.05f
    val fill = when {
        !enabled -> scheme.onSurface.copy(alpha = 0.12f)
        solid -> container
        else -> scheme.primary
    }
    val onFill = when {
        !enabled -> scheme.onSurface.copy(alpha = 0.38f)
        solid -> base.contentColor
        else -> scheme.onPrimary
    }
    val accent = when {
        !enabled -> scheme.onSurface.copy(alpha = 0.38f)
        solid -> container
        else -> base.contentColor
    }
    fun look(modifier: Modifier, content: Color, border: BorderStroke?) = ButtonLook(
        modifier = modifier,
        colors = base.copy(
            containerColor = Color.Transparent, disabledContainerColor = Color.Transparent,
            contentColor = content, disabledContentColor = content
        ),
        border = border, flat = true
    )
    fun stroke(c: Color, defaultDp: Float): BorderStroke? {
        val w = widthDp ?: defaultDp
        return if (w <= 0f) null else BorderStroke(w.dp, c)
    }
    return when (variant) {
        ButtonVariant.FILLED, ButtonVariant.DEFAULT -> look(Modifier.background(fill, shape), onFill, null)
        ButtonVariant.OUTLINE -> look(Modifier, accent, stroke(accent, 1.5f))
        ButtonVariant.SOFT -> look(
            Modifier.background(accent.copy(alpha = 0.16f), shape),
            if (enabled) lerp(accent, scheme.onSurface, 0.25f) else accent, null
        )
        ButtonVariant.GEL -> look(
            Modifier.background(
                Brush.verticalGradient(
                    0f to lerp(fill, Color.White, 0.45f),
                    0.48f to lerp(fill, Color.White, 0.10f),
                    0.52f to fill,
                    1f to lerp(fill, Color.Black, 0.14f)
                ), shape
            ),
            onFill, stroke(lerp(fill, Color.Black, 0.3f), 1f)
        )
        ButtonVariant.RAISED3D -> {
            val ledge = lerp(fill, Color.Black, 0.38f)
            look(
                Modifier
                    .drawBehind {
                        val l = 3.dp.toPx()
                        val outline = shape.createOutline(Size(size.width, size.height - l), layoutDirection, this)
                        translate(0f, l) { drawOutline(outline, ledge) }
                    }
                    .padding(bottom = 3.dp)
                    .background(fill, shape),
                onFill, null
            )
        }
        ButtonVariant.UNDERLINE -> look(
            Modifier.drawBehind {
                val t = (widthDp ?: 2f).coerceAtLeast(1f).dp.toPx()
                val inset = 10.dp.toPx()
                drawLine(accent, Offset(inset, size.height - t / 2), Offset(size.width - inset, size.height - t / 2), t)
            },
            accent, null
        )
        ButtonVariant.BRACKET -> look(
            Modifier.drawBehind {
                val t = (widthDp ?: 1.5f).coerceAtLeast(1f).dp.toPx()
                val a = 7.dp.toPx()
                val m = t / 2 + 1.dp.toPx()
                val h = size.height
                val w = size.width
                val left = Path().apply { moveTo(m + a, m); lineTo(m, m); lineTo(m, h - m); lineTo(m + a, h - m) }
                val right = Path().apply { moveTo(w - m - a, m); lineTo(w - m, m); lineTo(w - m, h - m); lineTo(w - m - a, h - m) }
                drawPath(left, accent, style = Stroke(t))
                drawPath(right, accent, style = Stroke(t))
            },
            accent, null
        )
    }
}

/**
 * Buttons without an explicit shape follow the theme's corner style (square in CLASSIC/PIXEL/TERMINAL,
 * cut in cut-corner themes). With the stock rounded style and scale 1 the Material default is kept.
 */
@Composable
internal fun defaultButtonShape(stock: Shape): Shape {
    val extras = LocalThemeExtras.current
    if (extras.shapeStyle == ThemeShapeStyle.ROUNDED &&
        (extras.uiStyle == UiStyle.CLASSIC || extras.uiStyle == UiStyle.PIXEL || extras.uiStyle == UiStyle.TERMINAL)
    ) return RectangleShape
    val themed = neonButtonShape()
    return if (themed == ButtonDefaults.shape) stock else themed
}

/**
 * Material silently grows buttons/chips to a 48dp touch target. Painted styles draw on the outer
 * modifier, so that growth would make the painting bigger than the control ("swollen" buttons and a
 * second button showing behind chips). When a style paints, the expansion is turned off.
 */
@Composable
internal fun StyledTouchTarget(styled: Boolean, content: @Composable () -> Unit) {
    if (!styled) {
        content()
        return
    }
    androidx.compose.runtime.CompositionLocalProvider(
        androidx.compose.material3.LocalMinimumInteractiveComponentSize provides androidx.compose.ui.unit.Dp.Unspecified,
        content = content
    )
}

private fun Color.compositeWhite(amount: Float) = lerp(this, Color.White.copy(alpha = this.alpha), amount)

@Composable
fun Button(
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
    enabled: Boolean = true,
    shape: Shape = defaultButtonShape(ButtonDefaults.shape),
    colors: ButtonColors = ButtonDefaults.buttonColors(),
    elevation: ButtonElevation? = ButtonDefaults.buttonElevation(),
    border: BorderStroke? = null,
    contentPadding: PaddingValues = ButtonDefaults.ContentPadding,
    interactionSource: MutableInteractionSource? = null,
    content: @Composable RowScope.() -> Unit
) {
    val source = interactionSource ?: remember { MutableInteractionSource() }
    val look = buttonLook(LocalThemeExtras.current.uiStyle, colors, enabled, shape, border, source, filled = true)
    StyledTouchTarget(look != null) {
        M3Button(onClick, modifier.then(look?.modifier ?: Modifier), enabled, shape, look?.colors ?: colors,
            if (look?.flat == true) null else elevation, look?.border ?: border, contentPadding, source, content)
    }
}

@Composable
fun FilledTonalButton(
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
    enabled: Boolean = true,
    shape: Shape = defaultButtonShape(ButtonDefaults.filledTonalShape),
    colors: ButtonColors = ButtonDefaults.filledTonalButtonColors(),
    elevation: ButtonElevation? = ButtonDefaults.filledTonalButtonElevation(),
    border: BorderStroke? = null,
    contentPadding: PaddingValues = ButtonDefaults.ContentPadding,
    interactionSource: MutableInteractionSource? = null,
    content: @Composable RowScope.() -> Unit
) {
    val source = interactionSource ?: remember { MutableInteractionSource() }
    val look = buttonLook(LocalThemeExtras.current.uiStyle, colors, enabled, shape, border, source, filled = true)
    StyledTouchTarget(look != null) {
        M3FilledTonalButton(onClick, modifier.then(look?.modifier ?: Modifier), enabled, shape, look?.colors ?: colors,
            if (look?.flat == true) null else elevation, look?.border ?: border, contentPadding, source, content)
    }
}

@Composable
fun OutlinedButton(
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
    enabled: Boolean = true,
    shape: Shape = defaultButtonShape(ButtonDefaults.outlinedShape),
    colors: ButtonColors = ButtonDefaults.outlinedButtonColors(),
    elevation: ButtonElevation? = null,
    border: BorderStroke? = ButtonDefaults.outlinedButtonBorder(enabled),
    contentPadding: PaddingValues = ButtonDefaults.ContentPadding,
    interactionSource: MutableInteractionSource? = null,
    content: @Composable RowScope.() -> Unit
) {
    val source = interactionSource ?: remember { MutableInteractionSource() }
    val look = buttonLook(LocalThemeExtras.current.uiStyle, colors, enabled, shape, border, source, filled = false)
    StyledTouchTarget(look != null) {
        M3OutlinedButton(onClick, modifier.then(look?.modifier ?: Modifier), enabled, shape, look?.colors ?: colors,
            elevation, if (look != null) look.border else border, contentPadding, source, content)
    }
}

@Composable
fun TextButton(
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
    enabled: Boolean = true,
    shape: Shape = defaultButtonShape(ButtonDefaults.textShape),
    colors: ButtonColors = ButtonDefaults.textButtonColors(),
    elevation: ButtonElevation? = null,
    border: BorderStroke? = null,
    contentPadding: PaddingValues = ButtonDefaults.TextButtonContentPadding,
    interactionSource: MutableInteractionSource? = null,
    content: @Composable RowScope.() -> Unit
) {
    val source = interactionSource ?: remember { MutableInteractionSource() }
    // Only the "raised button" styles give text buttons a body (classic dialogs had real buttons).
    val style = LocalThemeExtras.current.uiStyle
    val variant = LocalThemeExtras.current.button
    val look = if (style == UiStyle.CLASSIC || style == UiStyle.LUNA || style == UiStyle.PIXEL ||
        variant == ButtonVariant.UNDERLINE || variant == ButtonVariant.BRACKET
    ) {
        buttonLook(style, colors, enabled, shape, border, source, filled = false, text = true)
    } else null
    StyledTouchTarget(look != null) {
        M3TextButton(onClick, modifier.then(look?.modifier ?: Modifier), enabled, shape, look?.colors ?: colors,
            elevation, look?.border ?: border, contentPadding, source, content)
    }
}

package com.sbro.emucorev.ui.theme.style

import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.CutCornerShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.MaterialTheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.remember
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.drawBehind
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Canvas
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.ImageBitmap
import androidx.compose.ui.graphics.ImageShader
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.RectangleShape
import androidx.compose.ui.graphics.Shape
import androidx.compose.ui.graphics.ShaderBrush
import androidx.compose.ui.graphics.TileMode
import androidx.compose.ui.graphics.drawscope.CanvasDrawScope
import androidx.compose.ui.graphics.drawscope.DrawScope
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.graphics.drawOutline
import androidx.compose.ui.graphics.drawscope.translate
import androidx.compose.ui.graphics.lerp
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.unit.Density
import androidx.compose.ui.unit.LayoutDirection
import androidx.compose.ui.unit.dp
import com.sbro.emucorev.data.theme.AccentEdge
import com.sbro.emucorev.data.theme.BackgroundPattern
import com.sbro.emucorev.data.theme.CardVariant
import com.sbro.emucorev.data.theme.IconContainer
import com.sbro.emucorev.ui.theme.LocalThemeExtras
import kotlin.math.PI
import kotlin.math.sin
import kotlin.math.sqrt

/*
 * Structural theme dimensions (card / border / accent edge / pattern / icon container). They are
 * orthogonal to UiStyle: every field defaults to "keep the style's own look".
 */

/** Overlays are only drawn on real cards, not on small icon boxes or chips. */
private fun DrawScope.cardSized(): Boolean = size.height >= 40.dp.toPx() && size.width >= 96.dp.toPx()

@Composable
internal fun structuredPanelLook(
    base: PanelLook,
    shape: Shape,
    color: Color,
    border: BorderStroke?,
    shadowElevation: androidx.compose.ui.unit.Dp
): PanelLook {
    val extras = LocalThemeExtras.current
    if (extras.card == CardVariant.DEFAULT && extras.borderWidthDp == null && extras.accentEdge == AccentEdge.NONE) return base
    if (color.alpha <= 0.04f) return base
    val scheme = MaterialTheme.colorScheme
    val widthDp = extras.borderWidthDp
    fun stroke(c: Color, defaultDp: Float): BorderStroke? {
        val w = widthDp ?: defaultDp
        return if (w <= 0f) null else BorderStroke(w.dp, c)
    }
    fun rewidth(b: BorderStroke?): BorderStroke? = when {
        b == null || widthDp == null -> b
        widthDp <= 0f -> null
        else -> BorderStroke(widthDp.dp, b.brush)
    }
    val opaque = color.alpha > 0.98f
    val overlays = ArrayList<DrawScope.() -> Unit>(3)
    var look = base
    val outline = scheme.outline
    when (extras.card) {
        CardVariant.DEFAULT -> {
            if (widthDp != null) {
                look = PanelLook(base.modifier, base.color, rewidth(base.border), base.shadowElevation, base.tonalElevation)
            }
        }
        CardVariant.ELEVATED -> look = PanelLook(Modifier, color, rewidth(border),
            if (opaque) 6.dp else 0.dp, 0.dp)
        CardVariant.OUTLINED -> look = PanelLook(Modifier, color.copy(alpha = color.alpha * 0.35f), stroke(outline, 1.5f), 0.dp, 0.dp)
        CardVariant.FLAT -> look = PanelLook(Modifier, color, null, 0.dp, 0.dp)
        CardVariant.GLASS -> {
            look = PanelLook(Modifier, color.copy(alpha = color.alpha * 0.5f),
                stroke(lerp(outline, Color.White, 0.55f).copy(alpha = 0.5f), 1f), 0.dp, 0.dp)
            overlays += { drawRect(Brush.verticalGradient(0f to Color.White.copy(alpha = 0.16f), 0.5f to Color.Transparent)) }
        }
        CardVariant.INSET -> {
            look = PanelLook(Modifier, lerp(color, Color.Black, 0.14f).copy(alpha = color.alpha), stroke(outline.copy(alpha = 0.5f), 1f), 0.dp, 0.dp)
            overlays += {
                drawRect(Brush.verticalGradient(0f to Color.Black.copy(alpha = 0.22f), 0.14f to Color.Transparent))
                drawRect(Brush.horizontalGradient(0f to Color.Black.copy(alpha = 0.12f), 0.05f to Color.Transparent))
            }
        }
        CardVariant.NOTCHED -> {
            look = PanelLook(Modifier, color, stroke(outline.copy(alpha = 0.7f), 1f), 0.dp, 0.dp)
            val accent = extras.accent1
            overlays += {
                if (cardSized()) {
                    val s = 16.dp.toPx()
                    val tri = Path().apply {
                        moveTo(size.width - s, 0f); lineTo(size.width, 0f); lineTo(size.width, s); close()
                    }
                    drawPath(tri, accent)
                }
            }
        }
        CardVariant.DOUBLE -> {
            look = PanelLook(Modifier, color, stroke(outline, 1.5f), 0.dp, 0.dp)
            val inner = outline.copy(alpha = 0.6f)
            val w = (widthDp ?: 1.5f).coerceAtLeast(1f)
            overlays += {
                if (cardSized()) {
                    val gap = 4.dp.toPx()
                    val innerSize = Size(size.width - 2 * gap, size.height - 2 * gap)
                    val o = shape.createOutline(innerSize, LayoutDirection.Ltr, this)
                    translate(gap, gap) { drawOutline(o, inner, style = Stroke(w.dp.toPx())) }
                }
            }
        }
    }
    val edge = extras.accentEdge
    if (edge != AccentEdge.NONE) {
        val accent = extras.accent1
        overlays += {
            if (cardSized()) {
                val t = 3.dp.toPx()
                when (edge) {
                    AccentEdge.LEFT -> drawRect(accent, Offset.Zero, Size(t, size.height))
                    AccentEdge.TOP -> drawRect(accent, Offset.Zero, Size(size.width, t))
                    AccentEdge.BOTTOM -> drawRect(accent, Offset(0f, size.height - t), Size(size.width, t))
                    AccentEdge.NONE -> Unit
                }
            }
        }
    }
    if (overlays.isEmpty()) return look
    val list = overlays.toList()
    return PanelLook(
        modifier = look.modifier.then(Modifier.overlayInShape(shape) { list.forEach { it() } }),
        color = look.color, border = look.border, shadowElevation = look.shadowElevation, tonalElevation = look.tonalElevation
    )
}

/** Shape of the tinted tile behind a leading icon, or null when the theme asks for no tile. */
@Composable
fun themedIconContainerShape(default: Shape): Shape? = when (LocalThemeExtras.current.iconContainer) {
    IconContainer.DEFAULT -> default
    IconContainer.NONE -> null
    IconContainer.CIRCLE -> CircleShape
    IconContainer.SQUIRCLE -> RoundedCornerShape(35)
    IconContainer.SQUARE -> RoundedCornerShape(3.dp)
}

/** Tag-with-notch chip outline: cut on the trailing corners. */
internal val TagChipShape: Shape = CutCornerShape(topStart = 0.dp, topEnd = 10.dp, bottomEnd = 10.dp, bottomStart = 0.dp)

/** Subtle repeating pattern drawn above the screens: one cached tile, tiled by a shader. */
@Composable
fun ThemePatternOverlay(pattern: BackgroundPattern) {
    if (pattern == BackgroundPattern.NONE) return
    val density = LocalDensity.current
    val ink = MaterialTheme.colorScheme.onBackground
    val brush = remember(pattern, ink, density.density) {
        ShaderBrush(ImageShader(buildPatternTile(pattern, ink, density), TileMode.Repeated, TileMode.Repeated))
    }
    Box(Modifier.fillMaxSize().drawBehind { drawRect(brush) })
}

internal fun buildPatternTile(pattern: BackgroundPattern, ink: Color, density: Density): ImageBitmap {
    val d = density.density
    val (wDp, hDp) = when (pattern) {
        BackgroundPattern.DOTS -> 16f to 16f
        BackgroundPattern.GRID -> 24f to 24f
        BackgroundPattern.DIAGONAL -> 14f to 14f
        BackgroundPattern.WAVES -> 40f to 14f
        BackgroundPattern.NOISE -> 96f to 96f
        BackgroundPattern.CIRCUIT -> 64f to 64f
        BackgroundPattern.HEX -> 45f to (sqrt(3f) * 15f)
        BackgroundPattern.NONE -> 8f to 8f
    }
    val w = (wDp * d).toInt().coerceAtLeast(2)
    val h = (hDp * d).toInt().coerceAtLeast(2)
    val bitmap = ImageBitmap(w, h)
    val scope = CanvasDrawScope()
    scope.draw(density, LayoutDirection.Ltr, Canvas(bitmap), Size(w.toFloat(), h.toFloat())) {
        val line = 1.dp.toPx()
        val faint = ink.copy(alpha = 0.07f)
        when (pattern) {
            BackgroundPattern.DOTS -> drawCircle(ink.copy(alpha = 0.10f), radius = 1.2.dp.toPx(), center = Offset(size.width / 2, size.height / 2))
            BackgroundPattern.GRID -> {
                drawLine(faint, Offset(0f, 0f), Offset(size.width, 0f), line)
                drawLine(faint, Offset(0f, 0f), Offset(0f, size.height), line)
            }
            BackgroundPattern.DIAGONAL -> {
                drawLine(faint, Offset(0f, size.height), Offset(size.width, 0f), line)
                drawLine(faint, Offset(-size.width / 2, size.height / 2), Offset(size.width / 2, -size.height / 2), line)
                drawLine(faint, Offset(size.width / 2, size.height * 1.5f), Offset(size.width * 1.5f, size.height / 2), line)
            }
            BackgroundPattern.WAVES -> {
                val path = Path()
                val steps = 24
                for (i in 0..steps) {
                    val x = size.width * i / steps
                    val y = size.height / 2 + sin(2.0 * PI * i / steps).toFloat() * size.height * 0.28f
                    if (i == 0) path.moveTo(x, y) else path.lineTo(x, y)
                }
                drawPath(path, faint, style = Stroke(line))
            }
            BackgroundPattern.NOISE -> {
                val rnd = java.util.Random(7)
                repeat(120) {
                    drawRect(ink.copy(alpha = 0.04f + rnd.nextFloat() * 0.08f), Offset(rnd.nextInt(w).toFloat(), rnd.nextInt(h).toFloat()), Size(d, d))
                }
            }
            BackgroundPattern.CIRCUIT -> {
                val u = size.width / 4f
                val path = Path().apply {
                    moveTo(0f, u); lineTo(u * 2, u); lineTo(u * 2, u * 2); lineTo(u * 3, u * 2); lineTo(u * 3, u * 4)
                    moveTo(u * 4, u * 3); lineTo(u * 3.5f, u * 3); lineTo(u * 3.5f, u * 3.5f)
                    moveTo(u, 0f); lineTo(u, u * 0.6f)
                    moveTo(u * 0.5f, u * 4); lineTo(u * 0.5f, u * 3); lineTo(u * 1.5f, u * 3)
                }
                drawPath(path, faint, style = Stroke(line))
                val node = ink.copy(alpha = 0.12f)
                for (p in listOf(Offset(u * 2, u), Offset(u * 3, u * 2), Offset(u * 1.5f, u * 3), Offset(u, u * 0.6f))) {
                    drawCircle(node, radius = 1.8.dp.toPx(), center = p, style = Stroke(line))
                }
            }
            BackgroundPattern.HEX -> {
                val r = 15.dp.toPx()
                val hh = sqrt(3f) * r
                fun hex(cx: Float, cy: Float) {
                    val p = Path()
                    for (i in 0..5) {
                        val a = (PI / 3 * i).toFloat()
                        val x = cx + r * kotlin.math.cos(a)
                        val y = cy + r * sin(a)
                        if (i == 0) p.moveTo(x, y) else p.lineTo(x, y)
                    }
                    p.close()
                    drawPath(p, faint, style = Stroke(line))
                }
                hex(0f, 0f); hex(3 * r, 0f); hex(0f, hh); hex(3 * r, hh); hex(1.5f * r, hh / 2)
            }
            BackgroundPattern.NONE -> Unit
        }
    }
    return bitmap
}

/** Applies `typography.titleCase` to screen-header titles. */
@Composable
fun themedTitleCase(title: String): String =
    if (LocalThemeExtras.current.titleCase == com.sbro.emucorev.data.theme.TitleCase.UPPER) title.uppercase() else title

package com.sbro.emucorev.ui.theme.style

import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.heightIn
import androidx.compose.ui.Alignment
import androidx.compose.ui.draw.drawBehind
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.graphics.luminance
import com.sbro.emucorev.data.theme.HeaderVariant
import androidx.compose.material3.LocalContentColor
import androidx.compose.material3.MaterialTheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.drawWithContent
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Shape
import androidx.compose.ui.graphics.lerp
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import com.sbro.emucorev.data.theme.UiStyle
import com.sbro.emucorev.ui.theme.LocalThemeExtras

/** Header fill for "title bar" UI styles; null means the regular panel header is used. */
@Composable
fun styledHeaderBrush(): Brush? {
    val primary = MaterialTheme.colorScheme.primary
    val secondary = MaterialTheme.colorScheme.secondary
    val extras = LocalThemeExtras.current
    when (extras.header) {
        HeaderVariant.SOLID -> return Brush.verticalGradient(listOf(primary, primary))
        HeaderVariant.GRADIENT -> {
            val away = if (MaterialTheme.colorScheme.onPrimary.luminance() > 0.5f) Color.Black else Color.White
            return Brush.horizontalGradient(listOf(primary, lerp(primary, away, 0.25f)))
        }
        HeaderVariant.DEFAULT -> Unit
        // Transparent/outlined/glass/large-title headers are regular panel headers.
        else -> return null
    }
    return when (extras.uiStyle) {
        UiStyle.CLASSIC -> Brush.horizontalGradient(listOf(primary, lerp(primary, Color.White, 0.42f)))
        UiStyle.LUNA -> Brush.verticalGradient(
            0f to lerp(primary, Color.White, 0.45f),
            0.18f to lerp(primary, Color.White, 0.12f),
            0.8f to primary,
            1f to lerp(primary, Color.Black, 0.22f)
        )
        UiStyle.S60 -> Brush.verticalGradient(listOf(lerp(primary, Color.White, 0.28f), lerp(primary, Color.Black, 0.18f)))
        UiStyle.PIXEL -> Brush.verticalGradient(listOf(primary, primary))
        UiStyle.METRO -> Brush.verticalGradient(listOf(primary, primary))
        UiStyle.OSD -> Brush.horizontalGradient(listOf(lerp(primary, Color.Black, 0.55f).copy(alpha = 0.85f), lerp(secondary, Color.Black, 0.6f).copy(alpha = 0.55f)))
        else -> null
    }
}

@Composable
private fun headerFrame(): Modifier {
    val outline = MaterialTheme.colorScheme.outline
    val primary = MaterialTheme.colorScheme.primary
    if (LocalThemeExtras.current.header != HeaderVariant.DEFAULT) return Modifier
    return when (LocalThemeExtras.current.uiStyle) {
        UiStyle.CLASSIC -> Modifier.drawWithContent {
            drawContent()
            drawBevel(Color.White.copy(alpha = 0.95f), Color(0xFF404040), Color(0xFF808080), sunken = false)
        }
        UiStyle.PIXEL -> Modifier.drawWithContent {
            drawContent()
            drawPixelFrame(lerp(primary, Color.Black, 0.45f))
        }
        UiStyle.OSD -> Modifier.drawWithContent {
            drawContent()
            drawRect(primary.copy(alpha = 0.55f), style = androidx.compose.ui.graphics.drawscope.Stroke(1.dp.toPx()))
        }
        UiStyle.LUNA -> Modifier.drawWithContent {
            drawContent()
            drawRect(lerp(outline, Color.Black, 0.3f), style = androidx.compose.ui.graphics.drawscope.Stroke(1.dp.toPx()))
        }
        else -> Modifier
    }
}

/**
 * Surface for screen headers. Title-bar styles paint a filled header and remap the content colors
 * (onSurface/onSurfaceVariant) to readable "on header" colors, so any header content adapts.
 */
@Composable
fun HeaderSurface(
    modifier: Modifier = Modifier,
    shape: Shape,
    color: Color = MaterialTheme.colorScheme.surface,
    tonalElevation: Dp = 0.dp,
    shadowElevation: Dp = 0.dp,
    border: BorderStroke? = null,
    content: @Composable () -> Unit
) {
    val brush = styledHeaderBrush()
    if (brush == null) {
        val extras = LocalThemeExtras.current
        val scheme = MaterialTheme.colorScheme
        val widthDp = extras.borderWidthDp
        when (extras.header) {
            HeaderVariant.UNDERLINE -> {
                val rule = extras.accent1
                val t = (widthDp ?: 2f).coerceAtLeast(1f)
                Box(modifier = modifier.drawBehind {
                    drawLine(rule, Offset(0f, size.height - t.dp.toPx() / 2), Offset(size.width, size.height - t.dp.toPx() / 2), t.dp.toPx())
                }) { content() }
                return
            }
            HeaderVariant.OUTLINED -> {
                Surface(modifier = modifier, shape = shape, color = Color.Transparent,
                    border = BorderStroke(((widthDp ?: 1.5f).takeIf { it > 0f } ?: 1.5f).dp, scheme.primary), content = content)
                return
            }
            HeaderVariant.GLASS -> {
                Surface(
                    modifier = modifier.overlayInShape(shape) {
                        drawRect(Brush.verticalGradient(0f to Color.White.copy(alpha = 0.16f), 0.6f to Color.Transparent))
                    },
                    shape = shape, color = color.copy(alpha = color.alpha * 0.45f),
                    border = BorderStroke(1.dp, lerp(scheme.outline, Color.White, 0.55f).copy(alpha = 0.5f)), content = content
                )
                return
            }
            HeaderVariant.LARGE_TITLE -> {
                val typography = MaterialTheme.typography
                Surface(modifier = modifier, shape = shape, color = color, tonalElevation = tonalElevation,
                    shadowElevation = shadowElevation, border = border) {
                    // Heavier title only: a taller bar with bigger text truncated titles and swelled bars
                    // that carry several action icons (Hub, Settings).
                    MaterialTheme(
                        colorScheme = scheme, shapes = MaterialTheme.shapes,
                        typography = typography.copy(
                            titleLarge = typography.titleLarge.copy(fontWeight = androidx.compose.ui.text.font.FontWeight.ExtraBold),
                            titleMedium = typography.titleMedium.copy(fontWeight = androidx.compose.ui.text.font.FontWeight.ExtraBold)
                        )
                    ) {
                        content()
                    }
                }
                return
            }
            else -> Unit
        }
        Surface(modifier = modifier, shape = shape, color = color, tonalElevation = tonalElevation,
            shadowElevation = shadowElevation, border = border, content = content)
        return
    }
    val scheme = MaterialTheme.colorScheme
    val headerVariant = LocalThemeExtras.current.header
    val onHeader = if (headerVariant == HeaderVariant.DEFAULT && LocalThemeExtras.current.uiStyle == UiStyle.OSD) scheme.onSurface else scheme.onPrimary
    val headerScheme = scheme.copy(
        onSurface = onHeader,
        onSurfaceVariant = onHeader.copy(alpha = 0.82f),
        onBackground = onHeader,
        surfaceVariant = lerp(scheme.primary, Color.White, 0.18f),
        primary = onHeader,
        outlineVariant = onHeader.copy(alpha = 0.35f)
    )
    Box(modifier = modifier.clip(shape).background(brush).then(headerFrame())) {
        MaterialTheme(colorScheme = headerScheme, typography = MaterialTheme.typography, shapes = MaterialTheme.shapes) {
            // Menus opened from the header must use the page's colors, not the header remap.
            CompositionLocalProvider(LocalContentColor provides onHeader, LocalOuterColorScheme provides scheme) { content() }
        }
    }
}

/** The page color scheme outside a styled header (null when not inside one). */
val LocalOuterColorScheme = androidx.compose.runtime.compositionLocalOf<androidx.compose.material3.ColorScheme?> { null }

/**
 * Drop-in for material3.DropdownMenu. A menu opened from a title-bar header renders with the page's
 * normal colors instead of inheriting the header's remapped "on header" colors (white on white).
 */
@Composable
fun DropdownMenu(
    expanded: Boolean,
    onDismissRequest: () -> Unit,
    modifier: Modifier = Modifier,
    offset: androidx.compose.ui.unit.DpOffset = androidx.compose.ui.unit.DpOffset(0.dp, 0.dp),
    scrollState: androidx.compose.foundation.ScrollState = androidx.compose.foundation.rememberScrollState(),
    properties: androidx.compose.ui.window.PopupProperties = androidx.compose.ui.window.PopupProperties(focusable = true),
    shape: Shape? = null,
    containerColor: Color? = null,
    tonalElevation: Dp? = null,
    shadowElevation: Dp? = null,
    border: BorderStroke? = null,
    content: @Composable androidx.compose.foundation.layout.ColumnScope.() -> Unit
) {
    val outer = LocalOuterColorScheme.current
    @Composable
    fun menu() {
        androidx.compose.material3.DropdownMenu(
            expanded = expanded,
            onDismissRequest = onDismissRequest,
            modifier = modifier,
            offset = offset,
            scrollState = scrollState,
            properties = properties,
            shape = shape ?: androidx.compose.material3.MenuDefaults.shape,
            containerColor = containerColor ?: androidx.compose.material3.MenuDefaults.containerColor,
            tonalElevation = tonalElevation ?: androidx.compose.material3.MenuDefaults.TonalElevation,
            shadowElevation = shadowElevation ?: androidx.compose.material3.MenuDefaults.ShadowElevation,
            border = border,
            content = content
        )
    }
    if (outer == null) {
        menu()
    } else {
        MaterialTheme(colorScheme = outer, typography = MaterialTheme.typography, shapes = MaterialTheme.shapes) {
            CompositionLocalProvider(LocalContentColor provides outer.onSurface, LocalOuterColorScheme provides null) { menu() }
        }
    }
}

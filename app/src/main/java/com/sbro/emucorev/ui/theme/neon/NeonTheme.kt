package com.sbro.emucorev.ui.theme.neon

import androidx.compose.animation.core.LinearEasing
import androidx.compose.animation.core.RepeatMode
import androidx.compose.animation.core.animateFloat
import androidx.compose.animation.core.infiniteRepeatable
import androidx.compose.animation.core.rememberInfiniteTransition
import androidx.compose.animation.core.tween
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.CutCornerShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.ColorScheme
import androidx.compose.material3.FilterChipDefaults
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Shapes
import androidx.compose.material3.Text
import androidx.compose.material3.Typography
import androidx.compose.material3.darkColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.compositionLocalOf
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.Alignment
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.drawWithContent
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.Shape
import androidx.compose.ui.graphics.Shadow
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import kotlinx.coroutines.delay


// Cyberpunk 2077 palette (identical values to the reference implementation).
val NeonYellow = Color(0xFFFCEE0A)
val NeonRed = Color(0xFFFF003C)
val NeonBlue = Color(0xFF00F0FF)
val NeonBlack = Color(0xFF050505)
val NeonDark = Color(0xFF0F0F0F)
val NeonGray = Color(0xFF202020)

val NeonColorScheme: ColorScheme = darkColorScheme(
    primary = NeonYellow,
    onPrimary = NeonBlack,
    primaryContainer = Color(0xFF232107),
    onPrimaryContainer = NeonYellow,
    inversePrimary = Color(0xFF3B3600),
    secondary = NeonBlue,
    onSecondary = NeonBlack,
    secondaryContainer = Color(0xFF062327),
    onSecondaryContainer = NeonBlue,
    tertiary = NeonRed,
    onTertiary = NeonBlack,
    tertiaryContainer = Color(0xFF2B040D),
    onTertiaryContainer = Color(0xFFFF8AA0),
    background = NeonBlack,
    onBackground = Color(0xFFE8E8E4),
    surface = NeonDark,
    onSurface = Color(0xFFE8E8E4),
    surfaceVariant = NeonGray,
    onSurfaceVariant = Color(0xFFB4B4AC),
    surfaceTint = Color.Transparent,
    inverseSurface = Color(0xFFE8E8E4),
    inverseOnSurface = Color(0xFF141414),
    error = NeonRed,
    onError = NeonBlack,
    errorContainer = Color(0xFF2B040D),
    onErrorContainer = Color(0xFFFF8AA0),
    outline = Color(0xFF55513E),
    outlineVariant = Color(0xFF34322A),
    scrim = Color(0xE6000000),
    surfaceDim = NeonBlack,
    surfaceBright = Color(0xFF2A2A28),
    surfaceContainerLowest = Color(0xFF080808),
    surfaceContainerLow = Color(0xFF0E0E0E),
    surfaceContainer = Color(0xFF121212),
    surfaceContainerHigh = Color(0xFF1A1A1A),
    surfaceContainerHighest = Color(0xFF242424)
)

// Signature asymmetric cut corners (topEnd + bottomStart), exactly like the
// reference cyberpunk screens: buttons 10dp, panels 18dp.
val NeonShapes: Shapes = Shapes(
    extraSmall = CutCornerShape(topEnd = 4.dp, bottomStart = 4.dp),
    small = CutCornerShape(topEnd = 6.dp, bottomStart = 6.dp),
    medium = CutCornerShape(topEnd = 10.dp, bottomStart = 10.dp),
    large = CutCornerShape(topEnd = 14.dp, bottomStart = 14.dp),
    extraLarge = CutCornerShape(topEnd = 18.dp, bottomStart = 18.dp)
)

// Terminal-style monospace typeface across every role, keeping the app's sizes.
fun Typography.neonMonospace(): Typography = copy(
    displayLarge = displayLarge.neonMonoStyle(),
    displayMedium = displayMedium.neonMonoStyle(),
    displaySmall = displaySmall.neonMonoStyle(),
    headlineLarge = headlineLarge.neonMonoStyle(),
    headlineMedium = headlineMedium.neonMonoStyle(),
    headlineSmall = headlineSmall.neonMonoStyle(),
    titleLarge = titleLarge.neonMonoStyle(),
    titleMedium = titleMedium.neonMonoStyle(),
    titleSmall = titleSmall.neonMonoStyle(),
    bodyLarge = bodyLarge.neonMonoStyle(),
    bodyMedium = bodyMedium.neonMonoStyle(),
    bodySmall = bodySmall.neonMonoStyle(),
    labelLarge = labelLarge.neonMonoStyle(),
    labelMedium = labelMedium.neonMonoStyle(),
    labelSmall = labelSmall.neonMonoStyle()
)

private fun TextStyle.neonMonoStyle(): TextStyle = copy(fontFamily = FontFamily.Monospace)

// True while the Neon theme is active. Shared components read it to swap their
// hard-coded rounded shapes for the signature asymmetric cut corners and to add
// neon accents without changing anything for the other themes.
val LocalNeonTheme = compositionLocalOf { false }

// Rounded in every theme, asymmetric cyberpunk cut corners (topEnd + bottomStart)
// while Neon is active. Drop-in replacement for hard-coded RoundedCornerShape(size).
@Composable
fun neonShape(size: Dp): Shape = if (LocalNeonTheme.current) {
    CutCornerShape(topEnd = size, bottomStart = size)
} else {
    RoundedCornerShape(size)
}

// Cuts on the right side only (topEnd + bottomEnd) — used by top app bars.
@Composable
fun neonShapeRight(size: Dp): Shape = if (LocalNeonTheme.current) {
    CutCornerShape(topEnd = size, bottomEnd = size)
} else {
    RoundedCornerShape(size)
}

// Chip shape: pronounced cut corners in Neon, stock M3 chip shape otherwise.
@Composable
fun neonChipShape(): Shape = if (LocalNeonTheme.current) {
    CutCornerShape(topEnd = 10.dp, bottomStart = 10.dp)
} else {
    FilterChipDefaults.shape
}

// Button shape: cut corners in Neon, stock M3 pill otherwise.
@Composable
fun neonButtonShape(): Shape = if (LocalNeonTheme.current) {
    CutCornerShape(topEnd = 10.dp, bottomStart = 10.dp)
} else {
    ButtonDefaults.shape
}

// Rotating accent cycle (yellow / cyan / red) used for corner decorations.
fun neonAccentColor(index: Int): Color =
    when (index % 3) {
        0 -> NeonYellow
        1 -> NeonBlue
        else -> NeonRed
    }

// Draws the signature corner decorations over a card/chip: a small filled
// triangle at the bottom-right corner and an accent bracket at the top-left.
// No-op outside the Neon theme.
@Composable
fun Modifier.neonCornerAccent(
    accent: Color,
    markSize: Dp = 8.dp,
    bracket: Boolean = false
): Modifier = if (!LocalNeonTheme.current) {
    this
} else {
    drawWithContent {
        drawContent()
        val s = markSize.toPx()
        val w = size.width
        val h = size.height
        val triangle = Path().apply {
            moveTo(w, h)
            lineTo(w - s, h)
            lineTo(w, h - s)
            close()
        }
        drawPath(triangle, color = accent)
        if (bracket) {
            val bracketLength = s * 1.6f
            val stroke = 2.dp.toPx()
            drawLine(accent, Offset.Zero, Offset(bracketLength, 0f), strokeWidth = stroke)
            drawLine(accent, Offset.Zero, Offset(0f, bracketLength), strokeWidth = stroke)
        }
    }
}

// Types the text out character by character with a trailing terminal cursor.
@Composable
fun NeonTypewriterText(
    text: String,
    style: TextStyle,
    modifier: Modifier = Modifier
) {
    var displayedText by remember(text) { mutableStateOf("") }
    LaunchedEffect(text) {
        displayedText = ""
        text.forEach { char ->
            displayedText += char
            delay(50)
        }
    }
    Text(text = "$displayedText" + "_", style = style, modifier = modifier)
}

// The cyberpunk "system notification" hero card: pulsing yellow border, accent
// tick at the top, typed-out system label, glowing headline and optional body.
@Composable
fun NeonSystemBanner(
    title: String? = null,
    modifier: Modifier = Modifier,
    body: String? = null,
    label: String = "SYSTEM_NOTIFICATION // PRIORITY_1"
) {
    val transition = rememberInfiniteTransition(label = "neon_hero")
    val borderAlpha by transition.animateFloat(
        initialValue = 0.5f,
        targetValue = 1f,
        animationSpec = infiniteRepeatable(tween(2000, easing = LinearEasing), RepeatMode.Reverse),
        label = "neon_hero_border"
    )
    // Exactly like top app bar — opposite corners 24.dp, 45°.
    val shape = neonShape(24.dp)
    Box(
        modifier = modifier
            .fillMaxWidth()
            .clip(shape)
            .background(NeonYellow.copy(alpha = 0.05f))
            .border(1.dp, NeonYellow.copy(alpha = borderAlpha), shape)
    ) {
        Canvas(modifier = Modifier.matchParentSize()) {
            drawLine(
                color = NeonYellow.copy(alpha = 0.3f),
                start = Offset.Zero,
                end = Offset(32.dp.toPx(), 0f),
                strokeWidth = 4.dp.toPx()
            )
        }
        Column(modifier = Modifier.padding(horizontal = 18.dp, vertical = 12.dp)) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Box(modifier = Modifier.size(6.dp).background(NeonRed))
                Spacer(modifier = Modifier.width(8.dp))
                NeonTypewriterText(
                    text = label,
                    style = MaterialTheme.typography.labelSmall.copy(
                        fontFamily = FontFamily.Monospace,
                        color = NeonRed
                    )
                )
            }
            if (!title.isNullOrBlank()) {
                Spacer(modifier = Modifier.height(10.dp))
                Text(
                    text = title.uppercase(),
                    style = MaterialTheme.typography.headlineSmall.copy(
                        fontWeight = FontWeight.Black,
                        fontFamily = FontFamily.Monospace,
                        color = NeonYellow,
                        letterSpacing = 1.sp,
                        shadow = Shadow(color = NeonYellow.copy(alpha = 0.7f), blurRadius = 14f)
                    )
                )
            }
            if (!body.isNullOrBlank()) {
                Spacer(modifier = Modifier.height(6.dp))
                Text(
                    text = body,
                    style = MaterialTheme.typography.bodyMedium.copy(
                        color = Color.White.copy(alpha = 0.8f),
                        fontFamily = FontFamily.Monospace,
                        lineHeight = 20.sp
                    )
                )
            }
        }
    }
}

// Corner-specific variant mirroring RoundedCornerShape(topStart, topEnd, bottomEnd, bottomStart).
@Composable
fun neonShapeCorners(
    topStart: Dp = 0.dp,
    topEnd: Dp = 0.dp,
    bottomEnd: Dp = 0.dp,
    bottomStart: Dp = 0.dp
): Shape = if (LocalNeonTheme.current) {
    CutCornerShape(topStart = topStart, topEnd = topEnd, bottomEnd = bottomEnd, bottomStart = bottomStart)
} else {
    RoundedCornerShape(
        topStart = topStart,
        topEnd = topEnd,
        bottomEnd = bottomEnd,
        bottomStart = bottomStart
    )
}

// The signature tricolor strip (yellow / red / cyan) used under cyberpunk top bars.
@Composable
fun NeonTricolorDivider(modifier: Modifier = Modifier, horizontalPadding: Dp = 0.dp) {
    Row(
        modifier = modifier
            .fillMaxWidth()
            .padding(horizontal = horizontalPadding)
            .height(2.dp)
    ) {
        Box(modifier = Modifier.weight(0.3f).fillMaxHeight().background(NeonYellow))
        Spacer(modifier = Modifier.width(4.dp))
        Box(modifier = Modifier.weight(0.5f).fillMaxHeight().background(NeonRed))
        Spacer(modifier = Modifier.width(4.dp))
        Box(modifier = Modifier.weight(0.2f).fillMaxHeight().background(NeonBlue))
    }
}

/**
 * Full-app CRT scanline drift, drawn over the content like the reference
 * implementation's ScanlinesEffect. Pointer-transparent: touches pass through.
 */
@Composable
fun NeonCrtOverlay(modifier: Modifier = Modifier) {
    val transition = rememberInfiniteTransition(label = "neon_crt")
    val drift by transition.animateFloat(
        initialValue = 0f,
        targetValue = 1f,
        animationSpec = infiniteRepeatable(tween(1500, easing = LinearEasing)),
        label = "neon_crt_drift"
    )
    Canvas(
        modifier = modifier
            .fillMaxSize()
            .alpha(0.05f)
    ) {
        val gap = 4.dp.toPx()
        val shift = drift * gap
        var y = shift - gap
        while (y < size.height) {
            drawLine(
                color = Color.White,
                start = Offset(0f, y),
                end = Offset(size.width, y),
                strokeWidth = 1f
            )
            y += gap
        }
    }
}

// --- Optional per-screen effects, ready for future wiring -----------------------

// Vertical page gradient used behind cyberpunk screens.
fun neonBackgroundBrush(): Brush = Brush.verticalGradient(
    listOf(NeonBlack, NeonDark, NeonBlack)
)

// 40dp tech grid (draw behind screen content).
@Composable
fun NeonGridCanvas(modifier: Modifier = Modifier) {
    Canvas(modifier = modifier.fillMaxSize()) {
        val gridSize = 40.dp.toPx()
        val lineColor = NeonGray.copy(alpha = 0.5f)
        var x = 0f
        while (x <= size.width) {
            drawLine(lineColor, Offset(x, 0f), Offset(x, size.height), 1f)
            x += gridSize
        }
        var y = 0f
        while (y <= size.height) {
            drawLine(lineColor, Offset(0f, y), Offset(size.width, y), 1f)
            y += gridSize
        }
    }
}

// Radial vignette (draw behind screen content).
@Composable
fun NeonVignette(modifier: Modifier = Modifier) {
    Box(
        modifier = modifier
            .fillMaxSize()
            .background(
                Brush.radialGradient(
                    colors = listOf(Color.Transparent, Color.Black.copy(alpha = 0.7f)),
                    center = Offset.Unspecified,
                    radius = 2000f
                )
            )
    )
}

// Three-layer neon glitch headline, the signature cyberpunk title treatment.
@Composable
fun NeonGlitchText(
    text: String,
    style: TextStyle,
    modifier: Modifier = Modifier,
    color: Color = NeonYellow
) {
    val baseStyle = style.copy(color = color)
    Box(modifier = modifier) {
        Text(
            text = text,
            style = baseStyle.copy(
                color = NeonBlue.copy(alpha = 0.25f),
                shadow = Shadow(color = NeonBlue.copy(alpha = 0.9f), blurRadius = 24f)
            )
        )
        Text(
            text = text,
            style = baseStyle.copy(
                color = NeonRed.copy(alpha = 0.18f),
                shadow = Shadow(color = NeonRed.copy(alpha = 0.8f), blurRadius = 14f)
            )
        )
        Text(
            text = text,
            style = baseStyle.copy(
                shadow = Shadow(color = NeonYellow.copy(alpha = 0.9f), blurRadius = 6f)
            )
        )
    }
}

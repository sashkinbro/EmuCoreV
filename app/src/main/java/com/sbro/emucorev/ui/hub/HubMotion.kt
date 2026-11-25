package com.sbro.emucorev.ui.hub

import android.provider.Settings
import androidx.compose.animation.animateColorAsState
import androidx.compose.animation.core.Animatable
import androidx.compose.animation.core.FastOutSlowInEasing
import androidx.compose.animation.core.Spring
import androidx.compose.animation.core.animateDpAsState
import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.animation.core.snap
import androidx.compose.animation.core.spring
import androidx.compose.animation.core.tween
import androidx.compose.foundation.background
import androidx.compose.ui.Alignment
import androidx.compose.ui.draw.clip
import androidx.compose.foundation.clickable
import androidx.compose.foundation.interaction.InteractionSource
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.interaction.collectIsPressedAsState
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.offset
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.material3.MaterialTheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.runtime.staticCompositionLocalOf
import androidx.compose.ui.Modifier
import androidx.compose.ui.focus.FocusRequester
import androidx.compose.ui.focus.focusRequester
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.luminance
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.IntOffset
import androidx.compose.ui.unit.dp
import com.sbro.emucorev.ui.common.gamepadFocusableCard
import com.sbro.emucorev.ui.theme.neon.neonShape
import com.sbro.emucorev.ui.theme.style.Surface
import kotlinx.coroutines.delay

/** False when the user turned system animations off (animator duration scale 0): every Hub animation then snaps. */
@Composable
internal fun rememberMotionEnabled(): Boolean {
    val resolver = LocalContext.current.contentResolver
    return remember(resolver) {
        runCatching { Settings.Global.getFloat(resolver, Settings.Global.ANIMATOR_DURATION_SCALE, 1f) != 0f }
            .getOrDefault(true)
    }
}

/**
 * Shared by everything that appears while the Hub changes screen: [direction] is +1 for forward navigation and -1 for
 * back, [shown] remembers which keys already played their entrance so scrolling does not replay it.
 */
internal class HubMotionState(val enabled: Boolean, val direction: Int, val shown: HashSet<String>)

internal val LocalHubMotion = staticCompositionLocalOf { HubMotionState(false, 1, HashSet()) }

/** Fade + short rise (and a sideways slide in the navigation direction) the first time [key] appears. */
@Composable
internal fun Modifier.enterRise(key: String, index: Int = 0): Modifier {
    val motion = LocalHubMotion.current
    val animate = remember { motion.enabled && motion.shown.add(key) }
    if (!animate) return this
    val progress = remember { Animatable(0f) }
    LaunchedEffect(Unit) {
        delay(index.coerceAtMost(3) * 30L)
        progress.animateTo(1f, tween(160, easing = FastOutSlowInEasing))
    }
    val direction = motion.direction
    return graphicsLayer {
        val remaining = 1f - progress.value
        alpha = progress.value
        translationX = direction * remaining * 20.dp.toPx()
        translationY = remaining * 8.dp.toPx()
    }
}

/** Springy 0.97 press feedback; only a graphicsLayer scale, no layout change. */
@Composable
internal fun Modifier.pressScale(interactionSource: InteractionSource): Modifier {
    if (!rememberMotionEnabled()) return this
    val pressed by interactionSource.collectIsPressedAsState()
    val scale = animateFloatAsState(
        targetValue = if (pressed) 0.97f else 1f,
        animationSpec = spring(dampingRatio = 0.6f, stiffness = Spring.StiffnessMedium),
        label = "press-scale"
    )
    return graphicsLayer {
        scaleX = scale.value
        scaleY = scale.value
    }
}

/**
 * Pill switch with equal segments and one indicator that slides between them. Outer and inner radii are concentric
 * (inner = outer - padding) so corners line up in every style.
 */
@Composable
internal fun SegmentedSwitch(
    count: Int,
    selectedIndex: Int,
    onSelected: (Int) -> Unit,
    segmentHeight: Dp,
    modifier: Modifier = Modifier,
    selectedFocusRequester: FocusRequester? = null,
    segment: @Composable (index: Int, selected: Boolean) -> Unit
) {
    val motion = rememberMotionEnabled()
    val gap = 4.dp
    val indicatorShape = neonShape(18.dp)
    // Styled themes (Classic, Luna, S60...) get a real panel track in the theme's own look, otherwise a bare outline
    // floats on saturated backgrounds. Standard/Neon keep the light outline (a gray slab looked heavy there).
    val style = com.sbro.emucorev.ui.theme.LocalThemeExtras.current.uiStyle
    val panelTrack = style != com.sbro.emucorev.data.theme.UiStyle.STANDARD && style != com.sbro.emucorev.data.theme.UiStyle.NEON
    val idleContent = if (panelTrack) MaterialTheme.colorScheme.onSurface else MaterialTheme.colorScheme.onBackground
    Surface(
        modifier = modifier.fillMaxWidth(),
        shape = neonShape(22.dp),
        color = if (panelTrack) MaterialTheme.colorScheme.surface else Color.Transparent,
        border = if (panelTrack) null
        else androidx.compose.foundation.BorderStroke(1.dp, MaterialTheme.colorScheme.onBackground.copy(alpha = 0.28f))
    ) {
        BoxWithConstraints(modifier = Modifier.padding(4.dp)) {
            val segmentWidth = (maxWidth - gap * (count - 1)) / count
            val x = animateDpAsState(
                targetValue = (segmentWidth + gap) * selectedIndex,
                animationSpec = if (motion) spring(dampingRatio = 0.8f, stiffness = Spring.StiffnessMediumLow) else snap(),
                label = "switch-indicator"
            )
            androidx.compose.foundation.layout.Box(
                Modifier
                    .offset { IntOffset(x.value.roundToPx(), 0) }
                    .width(segmentWidth)
                    .height(segmentHeight)
                    .background(MaterialTheme.colorScheme.primary, indicatorShape)
            )
            Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(gap)) {
                repeat(count) { index ->
                    val selected = index == selectedIndex
                    val interactionSource = remember { MutableInteractionSource() }
                    val shape = neonShape(18.dp)
                    val contentColor by animateColorAsState(
                        // Solid primary indicator + onBackground labels/outline: readable on any theme background
                        // (primaryContainer and outlineVariant vanished on saturated backgrounds like Ninety-Five).
                        targetValue = if (selected) MaterialTheme.colorScheme.onPrimary
                        else idleContent,
                        animationSpec = if (motion) tween(200) else snap(),
                        label = "switch-content"
                    )
                    // A bare tap target: the moving indicator is the only fill, so segments never read as
                    // separate cards (a Surface here picked up style fills and idle borders).
                    androidx.compose.foundation.layout.Box(
                        modifier = Modifier
                            .weight(1f)
                            .height(segmentHeight)
                            .then(if (selected && selectedFocusRequester != null) Modifier.focusRequester(selectedFocusRequester) else Modifier)
                            .gamepadFocusableCard(shape = shape, interactionSource = interactionSource, addFocusTarget = false, showIdleBorder = false)
                            .clip(shape)
                            .clickable(interactionSource = interactionSource, indication = androidx.compose.material3.ripple(), role = androidx.compose.ui.semantics.Role.Tab, onClick = { onSelected(index) }),
                        contentAlignment = Alignment.Center
                    ) {
                        androidx.compose.runtime.CompositionLocalProvider(androidx.compose.material3.LocalContentColor provides contentColor) {
                            segment(index, selected)
                        }
                    }
                }
            }
        }
    }
}

/** Accent for text links drawn straight on the screen background: primary when it reads (WCAG 3:1), else onBackground. */
@Composable
internal fun readableAccentOnBackground(accent: Color = MaterialTheme.colorScheme.primary): Color {
    val scheme = MaterialTheme.colorScheme
    val a = accent.luminance() + 0.05f
    val b = scheme.background.luminance() + 0.05f
    return if (maxOf(a, b) / minOf(a, b) >= 3f) accent else scheme.onBackground
}

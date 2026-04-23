package com.sbro.emucorev.ui.common

import androidx.compose.animation.core.FastOutSlowInEasing
import androidx.compose.animation.core.LinearEasing
import androidx.compose.animation.core.LinearOutSlowInEasing
import androidx.compose.animation.core.RepeatMode
import androidx.compose.animation.core.animateFloat
import androidx.compose.animation.core.infiniteRepeatable
import androidx.compose.animation.core.rememberInfiniteTransition
import androidx.compose.animation.core.tween
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.ui.composed
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp

fun Modifier.shimmer(
    showShimmer: Boolean = true
): Modifier = composed {
    val infiniteTransition = rememberInfiniteTransition(label = "shimmer")
    val translateAnimation by infiniteTransition.animateFloat(
        initialValue = -800f,
        targetValue = 1600f,
        animationSpec = infiniteRepeatable(
            animation = tween(1500, easing = LinearEasing),
            repeatMode = RepeatMode.Restart
        ),
        label = "shimmerTranslate"
    )

    if (showShimmer) {
        val shimmerColors = listOf(
            MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.5f),
            MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.9f),
            MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.5f),
        )

        val brush = Brush.linearGradient(
            colors = shimmerColors,
            start = Offset(translateAnimation, translateAnimation),
            end = Offset(translateAnimation + 400f, translateAnimation + 400f)
        )
        background(MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.3f))
            .background(brush)
    } else {
        this
    }
}

@Composable
fun PremiumLoadingAnimation(
    modifier: Modifier = Modifier,
    size: Dp = 64.dp,
    color: Color = MaterialTheme.colorScheme.primary
) {
    val infiniteTransition = rememberInfiniteTransition(label = "EmuCoreVLoading")

    val rotation by infiniteTransition.animateFloat(
        initialValue = 0f,
        targetValue = 360f,
        animationSpec = infiniteRepeatable(
            animation = tween(2200, easing = LinearEasing),
            repeatMode = RepeatMode.Restart
        ),
        label = "coreRotation"
    )

    val pulseScale by infiniteTransition.animateFloat(
        initialValue = 0.9f,
        targetValue = 1.35f,
        animationSpec = infiniteRepeatable(
            animation = tween(1100, easing = FastOutSlowInEasing),
            repeatMode = RepeatMode.Reverse
        ),
        label = "centerPulse"
    )

    val glowOpacity by infiniteTransition.animateFloat(
        initialValue = 0.25f,
        targetValue = 0.75f,
        animationSpec = infiniteRepeatable(
            animation = tween(1100, easing = LinearOutSlowInEasing),
            repeatMode = RepeatMode.Reverse
        ),
        label = "glowPulse"
    )

    Column(
        modifier = modifier,
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.spacedBy(32.dp)
    ) {
        Text(
            text = "EMUCOREV",
            style = MaterialTheme.typography.headlineSmall.copy(
                fontWeight = FontWeight.ExtraBold,
                letterSpacing = 6.sp,
                color = color.copy(alpha = glowOpacity)
            )
        )

        Box(
            modifier = Modifier.size(size),
            contentAlignment = Alignment.Center
        ) {
            Box(
                modifier = Modifier
                    .size(size * 1.0f)
                    .graphicsLayer {
                        scaleX = pulseScale * 1.4f
                        scaleY = pulseScale * 1.4f
                        alpha = glowOpacity * 0.45f
                    }
                    .background(color.copy(alpha = 0.25f), CircleShape)
            )

            Box(
                modifier = Modifier
                    .size(size * 0.82f)
                    .graphicsLayer { rotationZ = rotation }
                    .border(3.5.dp, color.copy(alpha = 0.85f), RoundedCornerShape(14.dp))
            )

            Box(
                modifier = Modifier
                    .size(size * 0.62f)
                    .graphicsLayer { rotationZ = -rotation * 1.8f }
                    .border(2.5.dp, color.copy(alpha = 0.55f), RoundedCornerShape(10.dp))
            )

            Text(
                text = "V",
                style = MaterialTheme.typography.headlineLarge.copy(
                    fontWeight = FontWeight.Black,
                    color = color
                ),
                modifier = Modifier.graphicsLayer {
                    val baseScale = 1.6f
                    scaleX = pulseScale * baseScale
                    scaleY = pulseScale * baseScale
                }
            )
        }
    }
}

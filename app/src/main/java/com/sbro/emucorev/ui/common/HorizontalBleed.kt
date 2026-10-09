package com.sbro.emucorev.ui.common

import androidx.compose.ui.Modifier
import androidx.compose.ui.layout.layout
import androidx.compose.ui.unit.Dp

fun Modifier.horizontalBleed(horizontalPadding: Dp): Modifier = layout { measurable, constraints ->
    val bleedPx = horizontalPadding.roundToPx()
    val looseConstraints = constraints.copy(
        maxWidth = (constraints.maxWidth + bleedPx * 2).coerceAtLeast(0)
    )
    val placeable = measurable.measure(looseConstraints)
    layout(constraints.maxWidth, placeable.height) {
        placeable.placeRelative(-bleedPx, 0)
    }
}

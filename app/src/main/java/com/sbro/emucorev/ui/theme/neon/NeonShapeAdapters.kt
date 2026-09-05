package com.sbro.emucorev.ui.theme.neon

import androidx.compose.foundation.shape.CutCornerShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.runtime.Composable
import androidx.compose.ui.graphics.Shape
import androidx.compose.ui.unit.dp

/** Preserves existing pill geometry outside Neon and uses its signature cut corners inside Neon. */
@Composable
fun neonPillShape(): Shape = if (LocalNeonTheme.current) {
    CutCornerShape(topEnd = 10.dp, bottomStart = 10.dp)
} else {
    RoundedCornerShape(999.dp)
}

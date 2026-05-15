package com.sbro.emucorev.ui.theme

import android.content.res.Configuration
import androidx.compose.ui.unit.dp

val ScreenHorizontalPadding = 10.dp
val ScreenTopInsetOffset = 8.dp
val ScreenContentBottomPadding = 110.dp
val CardContentPadding = 14.dp
val CompactCardContentPadding = 12.dp

fun Configuration.isTabletClassDevice(): Boolean {
    val smallestWidth = smallestScreenWidthDp.takeIf { it > 0 }
        ?: minOf(screenWidthDp, screenHeightDp)
    return smallestWidth >= 600
}

fun Configuration.shouldUseExpandedShell(): Boolean {
    return isTabletClassDevice() && screenWidthDp >= 840
}

/**
 * True when the current configuration warrants a multi-column / tablet-style
 * layout. Phones in landscape stay single-column — they are landscape but not
 * a tablet, so the extra horizontal space is treated as a wider single column
 * rather than reflowed into multi-pane content.
 */
fun Configuration.useMultiColumnLayout(): Boolean {
    return isTabletClassDevice() && screenWidthDp > screenHeightDp
}

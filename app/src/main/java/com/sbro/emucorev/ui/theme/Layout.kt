package com.sbro.emucorev.ui.theme

import android.content.res.Configuration
import androidx.compose.ui.unit.dp

val ScreenHorizontalPadding = 8.dp
val ScreenContentBottomPadding = 110.dp
val CardContentPadding = 14.dp
val CompactCardContentPadding = 12.dp

fun Configuration.isTabletClassDevice(): Boolean {
    val smallestWidth = smallestScreenWidthDp.takeIf { it > 0 }
        ?: minOf(screenWidthDp, screenHeightDp)
    val screenSize = screenLayout and Configuration.SCREENLAYOUT_SIZE_MASK
    val isAndroidLargeScreen = screenSize >= Configuration.SCREENLAYOUT_SIZE_LARGE
    val hasStableTabletWidth = smallestWidth >= 700
    val hasTabletLikeBounds = smallestWidth >= 600 &&
        minOf(screenWidthDp, screenHeightDp) >= 600 &&
        maxOf(screenWidthDp, screenHeightDp) >= 960
    return hasStableTabletWidth || (isAndroidLargeScreen && hasTabletLikeBounds)
}

fun Configuration.shouldUseExpandedShell(): Boolean {
    return isTabletClassDevice() &&
        screenWidthDp >= 900 &&
        screenHeightDp >= 600
}

/**
 * True when the current configuration warrants a multi-column / tablet-style
 * layout. Phones in landscape stay single-column — they are landscape but not
 * a tablet, so the extra horizontal space is treated as a wider single column
 * rather than reflowed into multi-pane content.
 */
fun Configuration.useMultiColumnLayout(): Boolean {
    return isTabletClassDevice() &&
        screenWidthDp > screenHeightDp &&
        screenHeightDp >= 600
}

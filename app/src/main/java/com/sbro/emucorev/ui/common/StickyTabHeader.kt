package com.sbro.emucorev.ui.common

import android.content.res.Configuration
import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.animation.core.tween
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.asPaddingValues
import androidx.compose.foundation.layout.displayCutout
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.lazy.LazyListState
import androidx.compose.foundation.lazy.grid.LazyGridState
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.runtime.Composable
import androidx.compose.runtime.derivedStateOf
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalConfiguration
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp

/**
 * Top inset for a pinned tab row. Portrait only needs to clear a display cutout (camera
 * hole/notch). Landscape ignores cutouts but keeps a small guard so scrolling near the top
 * edge does not pull down the system shade and clears rounded screen corners.
 */
@Composable
internal fun stickyTabTopInset(): Dp {
    val cutoutTop = WindowInsets.displayCutout
        .asPaddingValues()
        .calculateTopPadding()
    val isPortrait =
        LocalConfiguration.current.orientation == Configuration.ORIENTATION_PORTRAIT
    return if (isPortrait) cutoutTop else 16.dp
}

/** Opaque backing for a pinned tab row so scrolling content never shows through it. */
@Composable
internal fun StickyTabHeader(
    showDivider: Boolean,
    modifier: Modifier = Modifier,
    content: @Composable () -> Unit
) {
    val dividerAlpha by animateFloatAsState(
        targetValue = if (showDivider) 1f else 0f,
        animationSpec = tween(durationMillis = 180),
        label = "stickyTabDivider"
    )
    Surface(
        modifier = modifier.fillMaxWidth(),
        color = MaterialTheme.colorScheme.background
    ) {
        Column {
            content()
            HorizontalDivider(
                color = MaterialTheme.colorScheme.outlineVariant.copy(
                    alpha = 0.45f * dividerAlpha
                )
            )
        }
    }
}

/**
 * True while the tab header is pinned over scrolling content. Sticky headers are placed at
 * the viewport start offset when pinned, so their visible offset stops being positive.
 */
@Composable
internal fun rememberStickyTabHeaderPinned(
    listState: LazyListState,
    headerKey: Any
): Boolean {
    val pinned by remember(listState, headerKey) {
        derivedStateOf {
            val layoutInfo = listState.layoutInfo
            layoutInfo.visibleItemsInfo
                .firstOrNull { it.key == headerKey }
                ?.offset?.let { it <= layoutInfo.viewportStartOffset } == true
        }
    }
    return pinned
}

/** [rememberStickyTabHeaderPinned] for vertical grids such as the EmuCore hub. */
@Composable
internal fun rememberStickyTabHeaderPinned(
    gridState: LazyGridState,
    headerKey: Any
): Boolean {
    val pinned by remember(gridState, headerKey) {
        derivedStateOf {
            val layoutInfo = gridState.layoutInfo
            layoutInfo.visibleItemsInfo
                .firstOrNull { it.key == headerKey }
                ?.let { it.offset.y <= layoutInfo.viewportStartOffset } == true
        }
    }
    return pinned
}

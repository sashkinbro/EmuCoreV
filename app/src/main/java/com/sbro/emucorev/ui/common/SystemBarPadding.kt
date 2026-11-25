package com.sbro.emucorev.ui.common

import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.asPaddingValues
import androidx.compose.foundation.layout.calculateEndPadding
import androidx.compose.foundation.layout.calculateStartPadding
import androidx.compose.foundation.layout.displayCutout
import androidx.compose.foundation.layout.navigationBars
import androidx.compose.foundation.layout.statusBarsIgnoringVisibility
import androidx.compose.runtime.Composable
import androidx.compose.ui.platform.LocalLayoutDirection
import androidx.compose.ui.unit.Dp

@OptIn(ExperimentalLayoutApi::class)
@Composable
fun appStatusBarTopPadding(): Dp {
    val statusBarTop = WindowInsets.statusBarsIgnoringVisibility
        .asPaddingValues()
        .calculateTopPadding()
    val cutoutTop = WindowInsets.displayCutout
        .asPaddingValues()
        .calculateTopPadding()
    return maxOf(statusBarTop, cutoutTop)
}

@Composable
fun appScreenTopPadding(): Dp = appStatusBarTopPadding()

@Composable
fun navigationBarsHorizontalPaddingValues(): PaddingValues {
    val layoutDirection = LocalLayoutDirection.current
    val navigationBarsPadding = WindowInsets.navigationBars.asPaddingValues()
    return PaddingValues(
        start = navigationBarsPadding.calculateStartPadding(layoutDirection),
        end = navigationBarsPadding.calculateEndPadding(layoutDirection)
    )
}

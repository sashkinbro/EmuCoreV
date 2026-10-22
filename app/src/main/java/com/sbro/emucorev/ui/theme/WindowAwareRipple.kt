package com.sbro.emucorev.ui.theme

import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.LocalRippleConfiguration
import androidx.compose.material3.RippleConfiguration
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.staticCompositionLocalOf
import androidx.compose.ui.platform.LocalView

@OptIn(ExperimentalMaterial3Api::class)
private data class WindowRippleConfiguration(val value: RippleConfiguration?)

private val LocalWindowRippleConfiguration = staticCompositionLocalOf<WindowRippleConfiguration?> { null }

/**
 * Compose graphics layers can record hardware canvases even in a software window.
 * A platform ripple then tries to animate a RenderNode without an attached renderer.
 * Preserve the inherited configuration so a separate accelerated menu window can restore it.
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
internal fun WindowAwareRipple(content: @Composable () -> Unit) {
    val configuration = LocalWindowRippleConfiguration.current
        ?: WindowRippleConfiguration(LocalRippleConfiguration.current)
    CompositionLocalProvider(
        LocalWindowRippleConfiguration provides configuration,
        LocalRippleConfiguration provides if (LocalView.current.isHardwareAccelerated) configuration.value else null,
        content = content
    )
}

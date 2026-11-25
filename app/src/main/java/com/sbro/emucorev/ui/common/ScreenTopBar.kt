package com.sbro.emucorev.ui.common

import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.RowScope
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import com.sbro.emucorev.ui.theme.LocalThemeExtras
import com.sbro.emucorev.ui.theme.neon.LocalNeonTheme
import com.sbro.emucorev.ui.theme.style.HeaderSurface
import com.sbro.emucorev.ui.theme.style.styledHeaderBrush
import com.sbro.emucorev.ui.theme.style.themedTitleCase
import com.sbro.emucorev.data.theme.UiStyle
import androidx.compose.material3.LocalContentColor
import com.sbro.emucorev.ui.theme.neon.NeonTricolorDivider
import com.sbro.emucorev.ui.theme.neon.NeonYellow
import com.sbro.emucorev.ui.theme.neon.neonShape
import androidx.compose.material3.MaterialTheme
import com.sbro.emucorev.ui.theme.style.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Shadow
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp

@Composable
fun ScreenTopBarSurface(
    modifier: Modifier = Modifier,
    neonDecorated: Boolean = false,
    showNeonDivider: Boolean = false,
    content: @Composable RowScope.() -> Unit
) {
    val barSurface: @Composable (Modifier) -> Unit = { surfaceModifier ->
        HeaderSurface(
            modifier = surfaceModifier.fillMaxWidth(),
            shape = neonShape(24.dp),
            color = MaterialTheme.colorScheme.surface.copy(alpha = 0.78f),
            tonalElevation = 1.dp,
            shadowElevation = 0.dp,
            border = BorderStroke(
                1.dp,
                if (LocalNeonTheme.current) {
                    if (neonDecorated) {
                        LocalThemeExtras.current.accent1.copy(alpha = 0.35f)
                    } else {
                        MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.62f)
                    }
                } else {
                    MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.62f)
                }
            )
        ) {
            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(
                        start = 10.dp + if (LocalNeonTheme.current && !neonDecorated) 8.dp else 0.dp,
                        end = 10.dp,
                        top = 8.dp,
                        bottom = 8.dp
                    ),
                verticalAlignment = Alignment.CenterVertically,
                content = content
            )
        }
    }
    if (LocalNeonTheme.current && neonDecorated && showNeonDivider) {
        Column(modifier = modifier) {
            barSurface(Modifier)
            Spacer(modifier = Modifier.height(6.dp))
            NeonTricolorDivider(horizontalPadding = 10.dp)
        }
    } else {
        barSurface(modifier)
    }
}

@Composable
private fun defaultTopBarTitle(): Color =
    if (LocalNeonTheme.current) LocalThemeExtras.current.accent1 else MaterialTheme.colorScheme.onSurface

/** Terminal style shows headers as a prompt line. */
@Composable
private fun styledTitle(title: String): String =
    themedTitleCase(title).let {
        if (LocalThemeExtras.current.uiStyle == UiStyle.TERMINAL) "> ${it}_" else it
    }

@Composable
fun ScreenTopBar(
    title: String,
    modifier: Modifier = Modifier,
    subtitle: String? = null,
    onBackClick: (() -> Unit)? = null,
    onMenuClick: (() -> Unit)? = null,
    titleColor: Color = defaultTopBarTitle(),
    subtitleColor: Color = MaterialTheme.colorScheme.onSurfaceVariant,
    titleMaxLines: Int = 1,
    subtitleMaxLines: Int = 1,
    showNeonDivider: Boolean = false,
    actions: @Composable RowScope.() -> Unit = {}
) {
    val titleBarHeader = styledHeaderBrush() != null
    ScreenTopBarSurface(
        modifier = modifier,
        neonDecorated = true,
        showNeonDivider = showNeonDivider
    ) {
        // Title-bar styles paint the header in the theme's primary: the caller's colors were
        // resolved for a regular surface header, so use the header's own content color here.
        val onHeader = LocalContentColor.current
        when {
            onBackClick != null -> NavigationBackButton(onClick = onBackClick)
            onMenuClick != null -> NavigationMenuButton(onClick = onMenuClick)
        }
        if (onBackClick != null || onMenuClick != null) {
            Spacer(modifier = Modifier.width(14.dp))
        }
        Column(
            modifier = Modifier
                .weight(1f)
                .padding(end = 8.dp)
        ) {
            val effectiveTitleColor = if (titleBarHeader) onHeader else titleColor
            val neonGlowTitle = LocalNeonTheme.current && titleColor == LocalThemeExtras.current.accent1
            Text(
                text = styledTitle(title),
                style = if (neonGlowTitle) {
                    MaterialTheme.typography.titleLarge.copy(
                        fontWeight = FontWeight.Bold,
                        shadow = Shadow(
                            color = LocalThemeExtras.current.accent1.copy(alpha = 0.85f),
                            blurRadius = 18f
                        )
                    )
                } else {
                    MaterialTheme.typography.titleLarge.copy(fontWeight = FontWeight.Bold)
                },
                color = effectiveTitleColor,
                maxLines = titleMaxLines,
                overflow = TextOverflow.Ellipsis
            )
            if (!subtitle.isNullOrBlank()) {
                Text(
                    text = subtitle,
                    style = MaterialTheme.typography.bodyMedium,
                    color = if (titleBarHeader) onHeader.copy(alpha = 0.82f) else subtitleColor,
                    maxLines = subtitleMaxLines,
                    overflow = TextOverflow.Ellipsis
                )
            }
        }
        Row(
            horizontalArrangement = Arrangement.spacedBy(6.dp),
            verticalAlignment = Alignment.CenterVertically,
            content = actions
        )
    }
}

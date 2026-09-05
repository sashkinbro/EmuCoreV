package com.sbro.emucorev.ui.common

import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.size
import com.sbro.emucorev.ui.theme.neon.neonShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.Menu
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import com.sbro.emucorev.R

@Composable
private fun defaultMenuContainer(): Color =
    MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.54f)

@Composable
private fun defaultMenuContent(): Color =
    MaterialTheme.colorScheme.onSurfaceVariant

@Composable
fun NavigationMenuButton(
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
    containerColor: Color = defaultMenuContainer(),
    contentColor: Color = defaultMenuContent(),
    tonalElevation: Dp = 0.dp,
    shadowElevation: Dp = 0.dp
) {
    Surface(
        modifier = modifier.size(44.dp),
        shape = neonShape(14.dp),
        color = containerColor,
        tonalElevation = tonalElevation,
        shadowElevation = shadowElevation,
        border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.50f)),
        onClick = rememberDebouncedClick(onClick = onClick)
    ) {
        Box(contentAlignment = Alignment.Center) {
            Icon(
                imageVector = Icons.Rounded.Menu,
                contentDescription = stringResource(R.string.compatibility_menu),
                tint = contentColor,
                modifier = Modifier.size(19.dp)
            )
        }
    }
}

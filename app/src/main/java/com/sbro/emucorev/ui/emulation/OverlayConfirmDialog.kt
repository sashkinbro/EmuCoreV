package com.sbro.emucorev.ui.emulation

import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.MaterialTheme
import com.sbro.emucorev.ui.theme.style.Surface
import androidx.compose.material3.Text
import com.sbro.emucorev.ui.theme.style.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.focus.FocusRequester
import androidx.compose.ui.focus.focusRequester
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.unit.dp
import com.sbro.emucorev.ui.common.controllerFocus
import com.sbro.emucorev.ui.theme.neon.neonShape

/**
 * Confirmation dialog drawn inside the overlay composition instead of a separate Dialog window.
 * Separate windows over the immersive SurfaceView activity are unreliable on some OEM builds
 * (black screen / invisible dialog), so in-game confirmations use this scrim + card instead.
 * The confirm button takes initial focus so a controller can confirm; BACK is handled by the
 * overlay back handler (which must dismiss the owning state).
 */
@Composable
fun OverlayConfirmDialog(
    title: String,
    body: String,
    confirmText: String,
    dismissText: String,
    onConfirm: () -> Unit,
    onDismiss: () -> Unit,
    modifier: Modifier = Modifier
) {
    val confirmFocus = remember { FocusRequester() }
    LaunchedEffect(Unit) { runCatching { confirmFocus.requestFocus() } }
    val shape = neonShape(28.dp)
    Box(
        modifier = modifier
            .fillMaxSize()
            .background(Color.Black.copy(alpha = 0.6f))
            .clickable(
                interactionSource = remember { MutableInteractionSource() },
                indication = null,
                onClick = onDismiss
            ),
        contentAlignment = Alignment.Center
    ) {
        Surface(
            modifier = Modifier
                .padding(24.dp)
                .widthIn(max = 420.dp)
                .clickable(
                    interactionSource = remember { MutableInteractionSource() },
                    indication = null,
                    onClick = {}
                ),
            shape = shape,
            color = MaterialTheme.colorScheme.surface,
            contentColor = MaterialTheme.colorScheme.onSurface,
            tonalElevation = 6.dp
        ) {
            Column(
                modifier = Modifier.padding(24.dp).verticalScroll(rememberScrollState()),
                verticalArrangement = Arrangement.spacedBy(16.dp)
            ) {
                Text(text = title, style = MaterialTheme.typography.titleLarge)
                Text(
                    text = body,
                    style = MaterialTheme.typography.bodyLarge,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
                Row(
                    modifier = Modifier.align(Alignment.End),
                    horizontalArrangement = Arrangement.spacedBy(8.dp)
                ) {
                    TextButton(
                        onClick = onDismiss,
                        modifier = Modifier.controllerFocus(neonShape(16.dp))
                    ) { Text(text = dismissText) }
                    TextButton(
                        onClick = onConfirm,
                        modifier = Modifier
                            .focusRequester(confirmFocus)
                            .controllerFocus(neonShape(16.dp))
                    ) { Text(text = confirmText) }
                }
            }
        }
    }
}

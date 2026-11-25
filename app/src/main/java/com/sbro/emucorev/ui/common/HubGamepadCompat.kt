package com.sbro.emucorev.ui.common

import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Shape

// This app has no TV mode and no global shoulder-button router; the Hub and Store screens call these
// hooks so the screens stay identical to the sibling apps that do.

@Composable
@Suppress("UNUSED_PARAMETER")
fun ProvideGamepadShoulderActions(
    enabled: Boolean = true,
    onPrevious: () -> Unit,
    onNext: () -> Unit
) = Unit

@Suppress("UNUSED_PARAMETER")
fun Modifier.tvGamepadFocusableCard(
    shape: Shape? = null,
    interactionSource: MutableInteractionSource? = null,
    addFocusTarget: Boolean = true
): Modifier = this

fun Modifier.tvFocusGroup(): Modifier = this

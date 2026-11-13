package com.sbro.emucorev.ui.common

import android.app.Activity
import android.content.Context
import android.content.ContextWrapper
import android.view.View
import android.view.Window
import androidx.compose.material3.AlertDialogDefaults
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.SideEffect
import androidx.compose.runtime.remember
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Shape
import androidx.compose.ui.platform.LocalView
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.window.DialogProperties
import androidx.compose.ui.window.DialogWindowProvider

/** Keeps the platform Dialog and every caller's properties/layout/input contract intact. */
@Composable
fun AppDialog(
    onDismissRequest: () -> Unit,
    properties: DialogProperties = DialogProperties(),
    content: @Composable () -> Unit
) {
    val parentView = LocalView.current
    val holder = remember { DialogWindowBindingHolder() }
    androidx.compose.ui.window.Dialog(onDismissRequest, properties) {
        BindImmersiveDialogWindow(parentView, holder)
        content()
    }
    // Runs after Dialog's parameter update; does not reconstruct/drop any properties.
    SideEffect { holder.current?.refresh() }
}

/** Preserves stock Material alerts that intentionally do not use our styled scaffold. */
@Composable
fun ImmersiveMaterialAlertDialog(
    onDismissRequest: () -> Unit,
    confirmButton: @Composable () -> Unit,
    modifier: Modifier = Modifier,
    dismissButton: (@Composable () -> Unit)? = null,
    icon: (@Composable () -> Unit)? = null,
    title: (@Composable () -> Unit)? = null,
    text: (@Composable () -> Unit)? = null,
    shape: Shape = AlertDialogDefaults.shape,
    containerColor: Color = AlertDialogDefaults.containerColor,
    iconContentColor: Color = AlertDialogDefaults.iconContentColor,
    titleContentColor: Color = AlertDialogDefaults.titleContentColor,
    textContentColor: Color = AlertDialogDefaults.textContentColor,
    tonalElevation: Dp = AlertDialogDefaults.TonalElevation,
    properties: DialogProperties = DialogProperties()
) {
    val parentView = LocalView.current
    val holder = remember { DialogWindowBindingHolder() }
    androidx.compose.material3.AlertDialog(
        onDismissRequest = onDismissRequest,
        confirmButton = {
            BindImmersiveDialogWindow(parentView, holder)
            confirmButton()
        },
        modifier = modifier,
        dismissButton = dismissButton,
        icon = icon,
        title = title,
        text = text,
        shape = shape,
        containerColor = containerColor,
        iconContentColor = iconContentColor,
        titleContentColor = titleContentColor,
        textContentColor = textContentColor,
        tonalElevation = tonalElevation,
        properties = properties
    )
    SideEffect { holder.current?.refresh() }
}

private class DialogWindowBindingHolder {
    var current: ImmersiveDialogWindowBinding? = null
}

@Composable
private fun BindImmersiveDialogWindow(parentView: View, holder: DialogWindowBindingHolder) {
    val dialogView = LocalView.current
    val dialogWindow = dialogView.findDialogWindow()
    val parentWindow = remember(parentView) { parentView.findDialogWindow() ?: parentView.context.findActivityWindow() }
    DisposableEffect(dialogView, dialogWindow, parentView, parentWindow) {
        val binding = dialogWindow?.let { window ->
            ImmersiveDialogWindowBinding(window, restoreParent = {
                // Disposal can precede platform dismiss. Restore after the modal is gone.
                parentView.post {
                    if (parentView.isAttachedToWindow) parentWindow?.let(ImmersiveMode::applyToDialog)
                }
            })
        }
        holder.current = binding
        onDispose {
            // Dialog recreates for density/type/token changes. An old lease must not clear a new one.
            if (holder.current === binding) holder.current = null
            binding?.close()
        }
    }
}

private fun View.findDialogWindow(): Window? = generateSequence(this as Any?) {
    (it as? View)?.parent
}.filterIsInstance<DialogWindowProvider>().firstOrNull()?.window

private fun Context.findActivityWindow(): Window? = when (this) {
    is Activity -> window
    is ContextWrapper -> if (baseContext !== this) baseContext.findActivityWindow() else null
    else -> null
}

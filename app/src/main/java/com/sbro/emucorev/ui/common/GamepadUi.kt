package com.sbro.emucorev.ui.common

import android.hardware.input.InputManager
import android.content.Context
import android.os.Handler
import android.os.Looper
import com.sbro.emucorev.core.input.InputDeviceClassifier
import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.border
import androidx.compose.foundation.focusable
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.interaction.collectIsFocusedAsState
import androidx.compose.material3.MaterialTheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.composed
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.shadow
import androidx.compose.ui.focus.onFocusChanged
import androidx.compose.ui.focus.FocusRequester
import androidx.compose.ui.graphics.Shape
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.input.InputMode
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalInputModeManager
import androidx.compose.ui.unit.dp
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.LifecycleEventObserver
import androidx.lifecycle.compose.LocalLifecycleOwner
import com.sbro.emucorev.ui.theme.neon.neonShape

/** Observe hotplug as well as cold-start handheld controllers without changing input routing. */
@Composable
internal fun rememberControllerConnected(): Boolean {
    val context = LocalContext.current
    val manager = remember(context) { context.getSystemService(Context.INPUT_SERVICE) as InputManager }
    fun hasController() = manager.inputDeviceIds.any(InputDeviceClassifier::isPhysicalGameController)
    var connected by remember(manager) { mutableStateOf(hasController()) }
    DisposableEffect(manager) {
        val listener = object : InputManager.InputDeviceListener {
            private fun update(deviceId: Int) {
                InputDeviceClassifier.invalidateDevice(deviceId)
                connected = hasController()
            }
            override fun onInputDeviceAdded(deviceId: Int) { update(deviceId) }
            override fun onInputDeviceRemoved(deviceId: Int) { update(deviceId) }
            override fun onInputDeviceChanged(deviceId: Int) { update(deviceId) }
        }
        manager.registerInputDeviceListener(listener, Handler(Looper.getMainLooper()))
        connected = hasController()
        onDispose { manager.unregisterInputDeviceListener(listener) }
    }
    return connected
}

/** X focus scale, shadow and border; touch has no idle focus decoration. */
fun Modifier.gamepadFocusableCard(
    enabled: Boolean = true,
    shape: Shape? = null,
    interactionSource: MutableInteractionSource? = null,
    addFocusTarget: Boolean = true,
    showIdleBorder: Boolean = true
): Modifier = composed {
    val effectiveShape = shape ?: neonShape(18.dp)
    val source = interactionSource ?: remember { MutableInteractionSource() }
    val isFocused by source.collectIsFocusedAsState()
    val inputMode = LocalInputModeManager.current.inputMode
    val shouldHighlight = isFocused && enabled && inputMode == InputMode.Keyboard
    val scale by animateFloatAsState(if (shouldHighlight) 1.02f else 1f, label = "gamepadFocusScale")
    val border = when {
        shouldHighlight -> BorderStroke(2.dp, MaterialTheme.colorScheme.primary.copy(alpha = 0.95f))
        showIdleBorder -> BorderStroke(1.dp, MaterialTheme.colorScheme.onSurface.copy(alpha = 0.05f))
        else -> null
    }
    var result = this.graphicsLayer { scaleX = scale; scaleY = scale }
        .shadow(if (shouldHighlight) 14.dp else 0.dp, effectiveShape, clip = false)
        .clip(effectiveShape)
    if (border != null) result = result.border(border, effectiveShape)
    if (addFocusTarget) result.focusable(enabled, source) else result
}

@Composable
fun RequestFocusOnResume(focusRequester: FocusRequester, enabled: Boolean = true) {
    val lifecycleOwner = LocalLifecycleOwner.current
    DisposableEffect(lifecycleOwner, focusRequester, enabled) {
        val observer = LifecycleEventObserver { _, event ->
            if (enabled && event == Lifecycle.Event.ON_RESUME) {
                runCatching { focusRequester.requestFocus() }
            }
        }
        lifecycleOwner.lifecycle.addObserver(observer)
        onDispose { lifecycleOwner.lifecycle.removeObserver(observer) }
    }
}

/** X focus treatment for clickable Material surfaces that own their internal interaction source. */
fun Modifier.controllerFocus(shape: Shape, enabled: Boolean = true): Modifier = composed {
    var isFocused by remember { mutableStateOf(false) }
    val highlight = enabled && isFocused && LocalInputModeManager.current.inputMode == InputMode.Keyboard
    val scale by animateFloatAsState(if (highlight) 1.02f else 1f, label = "controllerFocusScale")
    var result = this.onFocusChanged { isFocused = it.isFocused }
        .graphicsLayer { scaleX = scale; scaleY = scale }
        .shadow(if (highlight) 14.dp else 0.dp, shape, clip = false)
    if (highlight) result = result.border(2.dp, MaterialTheme.colorScheme.primary.copy(alpha = 0.95f), shape)
    result
}

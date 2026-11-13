@file:OptIn(androidx.compose.foundation.layout.ExperimentalLayoutApi::class)
package com.sbro.emucorev.ui.emulation

import android.view.KeyEvent
import android.view.MotionEvent
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.DeleteOutline
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.input.pointer.pointerInteropFilter
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.platform.LocalView
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.IntOffset
import androidx.compose.ui.unit.dp
import com.sbro.emucorev.ui.common.AppDialog as Dialog
import com.sbro.emucorev.ui.common.ImmersiveMaterialAlertDialog as AlertDialog
import androidx.compose.ui.window.DialogProperties
import androidx.compose.ui.window.DialogWindowProvider
import com.sbro.emucorev.R
import com.sbro.emucorev.core.input.*
import com.sbro.emucorev.ui.common.premiumFilterChipColors
import com.sbro.emucorev.ui.theme.neon.neonShape
import com.sbro.emucorev.ui.theme.neon.neonChipShape
import com.sbro.emucorev.ui.theme.neon.neonButtonShape
import kotlin.math.roundToInt

/** Independent rear holds remain available even when the normal gamepad overlay is hidden. */
@Composable
fun RearTouchControls(profile: RearTouchProfile, input: RearTouchInput, modifier: Modifier = Modifier) {
    BoxWithConstraints(modifier.fillMaxSize()) {
        val width = with(LocalDensity.current) { maxWidth.toPx() }
        val height = with(LocalDensity.current) { maxHeight.toPx() }
        val buttonSize = with(LocalDensity.current) { 48.dp.toPx() }
        profile.points.filter { it.screenEnabled }.forEach { point ->
            var finger by remember(point.slot) { mutableIntStateOf(-1) }
            var startX by remember(point.slot) { mutableFloatStateOf(0f) }
            var startY by remember(point.slot) { mutableFloatStateOf(0f) }
            val source = "screen:${point.slot}"
            DisposableEffect(point.slot, input) {
                onDispose { input.dispatcher.setSource(point.slot, source, false) }
            }
            Surface(
                shape = neonShape(12.dp),
                color = if (finger >= 0) MaterialTheme.colorScheme.primaryContainer.copy(alpha = .9f)
                    else MaterialTheme.colorScheme.surface.copy(alpha = .7f),
                border = BorderStroke(1.dp, MaterialTheme.colorScheme.outline),
                modifier = Modifier.offset { IntOffset(
                    (point.screenX * width).coerceIn(0f, (width - buttonSize).coerceAtLeast(0f)).roundToInt(),
                    (point.screenY * height).coerceIn(0f, (height - buttonSize).coerceAtLeast(0f)).roundToInt()) }
                    .size(48.dp).testTag("rear_touch_${point.slot}")
                    .pointerInteropFilter { event ->
                        val index = event.actionIndex
                        when (event.actionMasked) {
                            MotionEvent.ACTION_DOWN, MotionEvent.ACTION_POINTER_DOWN -> if (finger < 0) {
                                finger = event.getPointerId(index)
                                startX = event.rawX; startY = event.rawY
                                input.dispatcher.movePoint(point.slot, point.x, point.y)
                                input.dispatcher.setSource(point.slot, source, true)
                            }
                            MotionEvent.ACTION_MOVE -> if (finger >= 0 && point.positionMode == RearPositionMode.SCREEN_DRAG && width > 0 && height > 0) {
                                val i = event.findPointerIndex(finger)
                                if (i >= 0) input.dispatcher.movePoint(point.slot,
                                    point.x + (event.getX(i) + event.rawX - event.x - startX) / width,
                                    point.y + (event.getY(i) + event.rawY - event.y - startY) / height)
                            }
                            MotionEvent.ACTION_UP, MotionEvent.ACTION_POINTER_UP -> if (event.getPointerId(index) == finger) {
                                finger = -1
                                input.dispatcher.setSource(point.slot, source, false)
                            }
                            MotionEvent.ACTION_CANCEL -> {
                                finger = -1
                                input.dispatcher.setSource(point.slot, source, false)
                            }
                        }
                        true
                    }
            ) {
                Box(contentAlignment = Alignment.Center) { Text("B${point.slot + 1}", color = androidx.compose.ui.graphics.Color.White, style = MaterialTheme.typography.labelLarge) }
            }
        }
    }
}

@Composable
fun RearTouchSettingsDialog(
    repository: RearTouchProfileRepository,
    titleId: String,
    input: RearTouchInput,
    onProfileChanged: (RearTouchProfile) -> Unit,
    onDismiss: () -> Unit
) {
    var gameProfile by remember(titleId) { mutableStateOf(titleId.isNotBlank()) }
    var profile by remember(titleId) { mutableStateOf(repository.effective(titleId)) }
    var inherit by remember(titleId) { mutableStateOf(repository.override(titleId) == null) }
    var slot by remember { mutableIntStateOf(0) }
    var capturing by remember { mutableStateOf(false) }
    val point = profile.normalized().points[slot]
    fun changed(next: RearTouchProfile) {
        profile = next.normalized()
        if (gameProfile) { repository.saveGame(titleId, profile); inherit = false }
        else repository.saveGlobal(profile)
        onProfileChanged(repository.effective(titleId))
    }
    fun updatePoint(next: RearTouchPoint) = changed(profile.copy(points = profile.points.map { if (it.slot == next.slot) next else it }))
    DisposableEffect(input) { onDispose { input.capture = null } }
    Dialog(onDismissRequest = onDismiss, properties = DialogProperties(usePlatformDefaultWidth = false)) {
        Surface(shape = neonShape(20.dp), tonalElevation = 4.dp,
            modifier = Modifier.widthIn(max = 720.dp).fillMaxWidth(.94f).fillMaxHeight(.94f)) {
            Column(Modifier.padding(20.dp).verticalScroll(rememberScrollState()), verticalArrangement = Arrangement.spacedBy(10.dp)) {
                Text(stringResource(R.string.rear_touch_title), style = MaterialTheme.typography.titleLarge)
                Text(stringResource(R.string.rear_touch_hint), style = MaterialTheme.typography.bodySmall)
                FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    FilterChip(shape = neonChipShape(), colors = premiumFilterChipColors(), selected = !gameProfile, onClick = { gameProfile = false; profile = repository.global() }, label = { Text(stringResource(R.string.rear_touch_global)) })
                    if (titleId.isNotBlank()) FilterChip(shape = neonChipShape(), colors = premiumFilterChipColors(), selected = gameProfile,
                        onClick = { gameProfile = true; profile = repository.effective(titleId); inherit = repository.override(titleId) == null },
                        label = { Text(stringResource(R.string.rear_touch_game)) })
                }
                if (gameProfile) Row(verticalAlignment = Alignment.CenterVertically) {
                    Text(stringResource(R.string.rear_touch_inherit), modifier = Modifier.weight(1f))
                    Switch(checked = inherit, onCheckedChange = { enabled ->
                        inherit = enabled
                        if (enabled) repository.inherit(titleId) else repository.saveGame(titleId, profile)
                        profile = repository.effective(titleId)
                        onProfileChanged(profile)
                    })
                }
                FlowRow(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                    profile.points.forEach { p ->
                        FilterChip(shape = neonChipShape(), colors = premiumFilterChipColors(), selected = p.slot == slot, onClick = { slot = p.slot }, label = { Text(stringResource(R.string.rear_touch_point, p.slot + 1)) })
                    }
                }
                Text(stringResource(R.string.rear_touch_position), style = MaterialTheme.typography.titleSmall)
                val primary = MaterialTheme.colorScheme.primary
                val outline = MaterialTheme.colorScheme.outline
                Canvas(Modifier.fillMaxWidth().height(120.dp).background(MaterialTheme.colorScheme.surfaceContainerLow, neonShape(8.dp))
                    .border(1.dp, outline, neonShape(8.dp))
                    .pointerInput(profile, slot) { detectTapGestures { offset ->
                        updatePoint(point.copy(x = offset.x / size.width, y = offset.y / size.height))
                    } }) {
                    profile.points.forEach { p ->
                        drawCircle(primary.copy(alpha = if (p.slot == slot) 1f else .35f), 9.dp.toPx(), Offset(p.x * size.width, p.y * size.height), style = Stroke(2.dp.toPx()))
                    }
                }
                CoordinateSlider("X", point.x) { updatePoint(point.copy(x = it)) }
                CoordinateSlider("Y", point.y) { updatePoint(point.copy(y = it)) }
                FlowRow(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                    RearPositionMode.entries.forEach { mode ->
                        val label = when (mode) {
                            RearPositionMode.FIXED -> R.string.rear_touch_fixed
                            RearPositionMode.SCREEN_DRAG -> R.string.rear_touch_drag
                            RearPositionMode.LEFT_STICK -> R.string.controls_button_left_stick
                            RearPositionMode.RIGHT_STICK -> R.string.controls_button_right_stick
                            RearPositionMode.POINTER -> R.string.rear_touch_pointer
                        }
                        FilterChip(shape = neonChipShape(), colors = premiumFilterChipColors(), selected = point.positionMode == mode, onClick = { updatePoint(point.copy(positionMode = mode)) }, label = { Text(stringResource(label)) })
                    }
                }
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Text(stringResource(R.string.rear_touch_screen), modifier = Modifier.weight(1f))
                    Switch(checked = point.screenEnabled, onCheckedChange = { updatePoint(point.copy(screenEnabled = it)) })
                }
                if (point.screenEnabled) {
                    Text(stringResource(R.string.rear_touch_screen_position), style = MaterialTheme.typography.titleSmall)
                    CoordinateSlider("X", point.screenX) { updatePoint(point.copy(screenX = it)) }
                    CoordinateSlider("Y", point.screenY) { updatePoint(point.copy(screenY = it)) }
                }
                point.bindings.forEach { binding ->
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        val label = when (binding.kind) {
                            RearBindingKind.KEY -> KeyEvent.keyCodeToString(binding.code).removePrefix("KEYCODE_").replace('_', ' ')
                            RearBindingKind.AXIS -> MotionEvent.axisToString(binding.code).removePrefix("AXIS_") + if (binding.direction < 0) " −" else " +"
                            RearBindingKind.MOUSE -> stringResource(R.string.rear_touch_pointer) + " ${binding.code}"
                        }
                        Text(label, modifier = Modifier.weight(1f))
                        IconButton(onClick = { updatePoint(point.copy(bindings = point.bindings - binding)) }) {
                            Icon(Icons.Rounded.DeleteOutline, contentDescription = stringResource(R.string.rear_touch_clear))
                        }
                    }
                }
                OutlinedButton(shape = neonButtonShape(), onClick = {
                    capturing = true
                    input.capture = { binding ->
                        updatePoint(point.copy(bindings = (point.bindings + binding).distinct()))
                        capturing = false
                    }
                }, enabled = point.bindings.size < 8) { Text(stringResource(R.string.rear_touch_bind)) }
                Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.End) {
                    TextButton(onClick = { changed(RearTouchProfile()) }) { Text(stringResource(R.string.emulation_controls_editor_reset)) }
                    TextButton(onClick = onDismiss) { Text(stringResource(R.string.common_close)) }
                }
            }
        }
    }
    if (capturing) AlertDialog(onDismissRequest = { capturing = false; input.capture = null },
        title = { Text(stringResource(R.string.rear_touch_bind)) }, text = { RearBindingCaptureWindow(input); Text(stringResource(R.string.rear_touch_capture)) },
        confirmButton = { TextButton(onClick = { capturing = false; input.capture = null }) { Text(stringResource(R.string.common_cancel)) } })
}

@Composable
private fun RearBindingCaptureWindow(input: RearTouchInput) {
    val view = LocalView.current
    val window = remember(view) { generateSequence(view.parent) { it.parent }.filterIsInstance<DialogWindowProvider>().firstOrNull()?.window }
    DisposableEffect(window, input) {
        val previous = window?.callback
        val callback = previous?.let { RearBindingWindowCallback(it, input) { window.decorView.width to window.decorView.height } }
        if (callback != null) window.callback = callback
        onDispose { if (callback != null && window.callback === callback) window.callback = previous }
    }
}

@Composable
private fun CoordinateSlider(label: String, value: Float, changed: (Float) -> Unit) {
    Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(8.dp)) {
        Text("$label ${(value * 100).roundToInt()}%", modifier = Modifier.width(64.dp), style = MaterialTheme.typography.labelMedium)
        Slider(value = value, onValueChange = changed, valueRange = 0f..1f, steps = 99, modifier = Modifier.weight(1f))
    }
}

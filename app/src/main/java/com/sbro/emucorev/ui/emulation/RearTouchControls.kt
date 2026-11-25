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
import com.sbro.emucorev.ui.theme.style.Surface
import com.sbro.emucorev.ui.theme.style.OutlinedButton
import com.sbro.emucorev.ui.theme.style.TextButton
import com.sbro.emucorev.ui.theme.style.FilterChip
import com.sbro.emucorev.ui.theme.style.Switch
import com.sbro.emucorev.ui.common.SyncedSlider
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.foundation.focusable
import androidx.compose.foundation.gestures.detectDragGestures
import androidx.compose.ui.input.key.Key
import androidx.compose.ui.input.key.KeyEventType
import androidx.compose.ui.input.key.key
import androidx.compose.ui.input.key.onPreviewKeyEvent
import androidx.compose.ui.input.key.type
import androidx.compose.ui.layout.onGloballyPositioned
import androidx.compose.ui.layout.positionInRoot
import androidx.compose.ui.text.style.TextOverflow
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
    val rootView = LocalView.current
    var origin by remember { mutableStateOf(Offset.Zero) }
    var contactTick by remember { mutableIntStateOf(0) }
    DisposableEffect(input) {
        input.dispatcher.contactListener = { contactTick++ }
        onDispose { input.dispatcher.contactListener = null }
    }
    BoxWithConstraints(modifier.fillMaxSize().onGloballyPositioned { coordinates ->
        val location = IntArray(2)
        rootView.getLocationOnScreen(location)
        origin = coordinates.positionInRoot() + Offset(location[0].toFloat(), location[1].toFloat())
    }) {
        val width = with(LocalDensity.current) { maxWidth.toPx() }
        val height = with(LocalDensity.current) { maxHeight.toPx() }
        val buttonSize = with(LocalDensity.current) { 48.dp.toPx() }
        profile.points.filter { it.screenEnabled }.forEach { point ->
            var finger by remember(point.slot) { mutableIntStateOf(-1) }
            var startX by remember(point.slot) { mutableFloatStateOf(0f) }
            var startY by remember(point.slot) { mutableFloatStateOf(0f) }
            val source = "screen:${point.slot}"
            val lit = finger >= 0 || (contactTick >= 0 && input.dispatcher.isHeld(point.slot))
            DisposableEffect(point.slot, input) {
                onDispose { input.dispatcher.setSource(point.slot, source, false) }
            }
            Surface(
                shape = neonShape(12.dp),
                color = if (lit) MaterialTheme.colorScheme.primaryContainer.copy(alpha = .9f)
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
                                // raw position of the pressing pointer (rawX/rawY alone is pointer 0)
                                startX = event.getX(index) + event.rawX - event.x
                                startY = event.getY(index) + event.rawY - event.y
                                val zoned = if (point.positionMode == RearPositionMode.ZONE) zonePosition(event, index, origin, width, height, input, profile.zone) else null
                                input.dispatcher.movePoint(point.slot, zoned?.first ?: point.x, zoned?.second ?: point.y)
                                input.dispatcher.setSource(point.slot, source, true)
                            }
                            MotionEvent.ACTION_MOVE -> if (finger >= 0 && (point.positionMode == RearPositionMode.SCREEN_DRAG || point.positionMode == RearPositionMode.ZONE) && width > 0 && height > 0) {
                                val i = event.findPointerIndex(finger)
                                if (i >= 0 && point.positionMode == RearPositionMode.ZONE) zonePosition(event, i, origin, width, height, input, profile.zone)?.let { input.dispatcher.movePoint(point.slot, it.first, it.second) }
                                else if (i >= 0) input.dispatcher.movePoint(point.slot,
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
                val textColor = if (lit) MaterialTheme.colorScheme.onPrimaryContainer else MaterialTheme.colorScheme.onSurface
                val hint = point.bindings.firstOrNull()?.let { rearBindingLabel(it) }
                Column(Modifier.padding(horizontal = 2.dp), horizontalAlignment = Alignment.CenterHorizontally, verticalArrangement = Arrangement.Center) {
                    Text("R${point.slot + 1}", color = textColor, style = MaterialTheme.typography.labelLarge)
                    if (hint != null) Text(hint, color = textColor, style = MaterialTheme.typography.labelSmall, maxLines = 1, overflow = TextOverflow.Ellipsis)
                }
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
    var confirmReset by remember { mutableStateOf(false) }
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
                Text(stringResource(R.string.rear_touch_trigger), style = MaterialTheme.typography.titleSmall)
                FlowRow(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                    RearTriggerMode.entries.forEach { mode ->
                        val label = when (mode) {
                            RearTriggerMode.HOLD -> R.string.rear_touch_trigger_hold
                            RearTriggerMode.TOGGLE -> R.string.rear_touch_trigger_toggle
                            RearTriggerMode.TAP -> R.string.rear_touch_trigger_tap
                        }
                        FilterChip(shape = neonChipShape(), colors = premiumFilterChipColors(), selected = point.trigger == mode,
                            onClick = { updatePoint(point.copy(trigger = mode)) }, label = { Text(stringResource(label)) })
                    }
                }
                Text(stringResource(R.string.rear_touch_position), style = MaterialTheme.typography.titleSmall)
                val primary = MaterialTheme.colorScheme.primary
                val outline = MaterialTheme.colorScheme.outline
                // The pad has the rear panel's own 1919x890 aspect, so preview and tap-to-place are undistorted.
                // Controller: focus the pad, press A/Enter to edit with the arrows, A/Enter again to leave.
                var padEditing by remember { mutableStateOf(false) }
                Canvas(Modifier.widthIn(max = 480.dp).fillMaxWidth().aspectRatio(REAR_PANEL_WIDTH / REAR_PANEL_HEIGHT)
                    .background(MaterialTheme.colorScheme.surfaceContainerLow, neonShape(8.dp))
                    .border(if (padEditing) 2.dp else 1.dp, if (padEditing) primary else outline, neonShape(8.dp))
                    .testTag("rear_touch_pad")
                    .onPreviewKeyEvent { e ->
                        if (e.type != KeyEventType.KeyDown) return@onPreviewKeyEvent false
                        val step = .02f
                        when (e.key) {
                            Key.DirectionCenter, Key.Enter, Key.ButtonA -> { padEditing = !padEditing; true }
                            Key.DirectionLeft -> padEditing.also { if (it) updatePoint(point.copy(x = point.x - step)) }
                            Key.DirectionRight -> padEditing.also { if (it) updatePoint(point.copy(x = point.x + step)) }
                            Key.DirectionUp -> padEditing.also { if (it) updatePoint(point.copy(y = point.y - step)) }
                            Key.DirectionDown -> padEditing.also { if (it) updatePoint(point.copy(y = point.y + step)) }
                            else -> false
                        }
                    }
                    .focusable()
                    .pointerInput(profile, slot) { detectTapGestures { offset ->
                        updatePoint(point.copy(x = offset.x / size.width, y = offset.y / size.height))
                    } }
                    .pointerInput(slot) { detectDragGestures { change, _ ->
                        updatePoint(point.copy(x = change.position.x / size.width, y = change.position.y / size.height))
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
                            RearPositionMode.ZONE -> R.string.rear_touch_zone_mode
                        }
                        FilterChip(shape = neonChipShape(), colors = premiumFilterChipColors(), selected = point.positionMode == mode, onClick = { updatePoint(point.copy(positionMode = mode)) }, label = { Text(stringResource(label)) })
                    }
                }
                if (point.positionMode == RearPositionMode.ZONE) {
                    Text(stringResource(R.string.rear_touch_zone_title), style = MaterialTheme.typography.titleSmall)
                    Text(stringResource(R.string.rear_touch_zone_hint), style = MaterialTheme.typography.bodySmall)
                    var dragStart by remember { mutableStateOf(Offset.Zero) }
                    Canvas(Modifier.widthIn(max = 480.dp).fillMaxWidth().aspectRatio(960f / 544f)
                        .background(MaterialTheme.colorScheme.surfaceContainerLow, neonShape(8.dp))
                        .border(1.dp, outline, neonShape(8.dp)).testTag("rear_touch_zone")
                        .pointerInput(profile) { detectDragGestures(
                            onDragStart = { dragStart = it },
                            onDrag = { change, _ -> changed(profile.copy(zone = rearZoneFromDrag(
                                dragStart.x / size.width, dragStart.y / size.height,
                                change.position.x / size.width, change.position.y / size.height))) }) }) {
                        val z = profile.zone
                        val zoneSize = androidx.compose.ui.geometry.Size((z.right - z.left) * size.width, (z.bottom - z.top) * size.height)
                        drawRect(primary.copy(alpha = .25f), Offset(z.left * size.width, z.top * size.height), zoneSize)
                        drawRect(primary, Offset(z.left * size.width, z.top * size.height), zoneSize, style = Stroke(2.dp.toPx()))
                    }
                    FlowRow(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                        RearZonePreset.entries.forEach { preset ->
                            val label = when (preset) {
                                RearZonePreset.FULL -> R.string.rear_touch_zone_full
                                RearZonePreset.LEFT_HALF -> R.string.rear_touch_zone_left
                                RearZonePreset.RIGHT_HALF -> R.string.rear_touch_zone_right
                                RearZonePreset.TOP_HALF -> R.string.rear_touch_zone_top
                                RearZonePreset.BOTTOM_HALF -> R.string.rear_touch_zone_bottom
                                RearZonePreset.CENTER -> R.string.rear_touch_zone_center
                            }
                            FilterChip(shape = neonChipShape(), colors = premiumFilterChipColors(), selected = RearZonePreset.match(profile.zone) == preset,
                                onClick = { changed(profile.copy(zone = preset.zone)) }, label = { Text(stringResource(label)) })
                        }
                    }
                }
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Text(stringResource(R.string.rear_touch_screen), modifier = Modifier.weight(1f))
                    Switch(checked = point.screenEnabled, onCheckedChange = { updatePoint(point.copy(screenEnabled = it)) })
                }
                if (point.screenEnabled) {
                    Text(stringResource(R.string.rear_touch_screen_position), style = MaterialTheme.typography.titleSmall)
                    // Drag (or tap) the button on the screen preview; the sliders stay for controller use.
                    Canvas(Modifier.widthIn(max = 480.dp).fillMaxWidth().aspectRatio(960f / 544f)
                        .background(MaterialTheme.colorScheme.surfaceContainerLow, neonShape(8.dp))
                        .border(1.dp, outline, neonShape(8.dp)).testTag("rear_touch_screen_pad")
                        .pointerInput(profile, slot) { detectTapGestures { offset ->
                            updatePoint(point.copy(screenX = offset.x / size.width, screenY = offset.y / size.height))
                        } }
                        .pointerInput(slot) { detectDragGestures { change, _ ->
                            updatePoint(point.copy(screenX = change.position.x / size.width, screenY = change.position.y / size.height))
                        } }) {
                        profile.points.filter { it.screenEnabled }.forEach { p ->
                            drawRoundRect(primary.copy(alpha = if (p.slot == slot) .9f else .35f), Offset(p.screenX * size.width, p.screenY * size.height),
                                androidx.compose.ui.geometry.Size(size.width * .05f, size.width * .05f),
                                androidx.compose.ui.geometry.CornerRadius(4.dp.toPx()))
                        }
                    }
                    CoordinateSlider("X", point.screenX) { updatePoint(point.copy(screenX = it)) }
                    CoordinateSlider("Y", point.screenY) { updatePoint(point.copy(screenY = it)) }
                }
                point.bindings.forEach { binding ->
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        Text(rearBindingLabel(binding), modifier = Modifier.weight(1f))
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
                    TextButton(onClick = { confirmReset = true }) { Text(stringResource(R.string.emulation_controls_editor_reset)) }
                    TextButton(onClick = onDismiss) { Text(stringResource(R.string.common_close)) }
                }
            }
        }
    }
    if (confirmReset) AlertDialog(onDismissRequest = { confirmReset = false },
        title = { Text(stringResource(R.string.emulation_controls_editor_reset)) },
        text = { Text(stringResource(R.string.rear_touch_reset_confirm)) },
        confirmButton = { TextButton(onClick = { confirmReset = false; changed(RearTouchProfile()) }) { Text(stringResource(R.string.emulation_controls_editor_reset)) } },
        dismissButton = { TextButton(onClick = { confirmReset = false }) { Text(stringResource(R.string.common_cancel)) } })
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
        SyncedSlider(value = value, onValueChange = changed, valueRange = 0f..1f, steps = 99, modifier = Modifier.weight(1f))
    }
}

@Composable
private fun rearBindingLabel(binding: RearTouchBinding): String = when (binding.kind) {
    RearBindingKind.KEY -> KeyEvent.keyCodeToString(binding.code).removePrefix("KEYCODE_").replace('_', ' ')
    RearBindingKind.AXIS -> MotionEvent.axisToString(binding.code).removePrefix("AXIS_") + if (binding.direction < 0) " -" else " +"
    RearBindingKind.MOUSE -> when (rearMouseButtonIndex(binding.code)) {
        0 -> stringResource(R.string.rear_touch_mouse_left)
        1 -> stringResource(R.string.rear_touch_mouse_right)
        2 -> stringResource(R.string.rear_touch_mouse_middle)
        3 -> stringResource(R.string.rear_touch_mouse_back)
        4 -> stringResource(R.string.rear_touch_mouse_forward)
        else -> stringResource(R.string.rear_touch_mouse_other, binding.code)
    }
}

/** Absolute rear position of a screen touch: its place on the visible Vita viewport mapped through [zone]. */
private fun zonePosition(event: MotionEvent, index: Int, origin: Offset, width: Float, height: Float, input: RearTouchInput, zone: RearZone): Pair<Float, Float>? {
    val rawX = event.getX(index) + event.rawX - event.x
    val rawY = event.getY(index) + event.rawY - event.y
    val p = rearPointerPosition(rawX - origin.x, rawY - origin.y, width.roundToInt(), height.roundToInt(), input.stretchDisplay) ?: return null
    return zone.map(p.first, p.second)
}

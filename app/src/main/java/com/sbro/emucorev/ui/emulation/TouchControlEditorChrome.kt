@file:OptIn(androidx.compose.foundation.layout.ExperimentalLayoutApi::class)

package com.sbro.emucorev.ui.emulation

import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import com.sbro.emucorev.R
import com.sbro.emucorev.ui.theme.neon.neonShape

/** The same toolbar / Tune-card hierarchy as EmuCoreX's ControlsEditorScreen. */
@Composable
internal fun TouchControlEditorChrome(
    selectedLabel: String, selectedVisible: Boolean, selectedScalePercent: Int,
    analogMode: TouchAnalogMode?, touchAreaWidthPercent: Int, touchAreaHeightPercent: Int,
    onReset: () -> Unit, onVisibilityToggle: () -> Unit,
    onSizeDecrease: () -> Unit, onSizeIncrease: () -> Unit, onAnalogModeToggle: () -> Unit,
    onTouchAreaWidthDecrease: () -> Unit, onTouchAreaWidthIncrease: () -> Unit,
    onTouchAreaHeightDecrease: () -> Unit, onTouchAreaHeightIncrease: () -> Unit,
    onDone: () -> Unit, showDimensions: Boolean,
    canDuplicate: Boolean, canDelete: Boolean, canCombo: Boolean, canCreate: Boolean,
    showGrid: Boolean, snapToGrid: Boolean,
    onDuplicate: () -> Unit, onDelete: () -> Unit, onCombo: () -> Unit, onCreate: () -> Unit,
    onResetSelected: () -> Unit, onGridToggle: () -> Unit, onSnapToggle: () -> Unit,
    modifier: Modifier = Modifier
) {
    var showAdjust by remember { mutableStateOf(false) }
    var confirmDelete by remember { mutableStateOf(false) }
    Column(
        modifier.widthIn(max = 760.dp).fillMaxWidth().testTag("controls_editor_panel")
            .verticalScroll(rememberScrollState()).padding(horizontal = 16.dp, vertical = 6.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.spacedBy(8.dp)
    ) {
        Surface(color = Color(0xFF2B3F93).copy(alpha = .88f), shape = neonShape(16.dp)) {
            Column(Modifier.padding(horizontal = 18.dp, vertical = 10.dp), horizontalAlignment = Alignment.CenterHorizontally) {
                Text(stringResource(R.string.emulation_controls_editor_title), color = Color.White,
                    style = MaterialTheme.typography.titleMedium, fontWeight = FontWeight.SemiBold)
                Text(stringResource(R.string.emulation_controls_editor_hint), color = Color.White.copy(alpha = .84f),
                    style = MaterialTheme.typography.labelMedium)
                Text(selectedLabel, color = Color.White.copy(alpha = .84f), style = MaterialTheme.typography.labelMedium)
            }
        }
        // Wrap rather than scroll sideways, so Done and every tool remain reachable in portrait.
        FlowRow(
            modifier = Modifier.testTag("controls_editor_toolbar"),
            horizontalArrangement = Arrangement.spacedBy(8.dp, Alignment.CenterHorizontally),
            verticalArrangement = Arrangement.spacedBy(4.dp)
        ) {
            EditorTool(Icons.Rounded.Refresh, stringResource(R.string.emulation_controls_editor_reset), "reset_all", onReset)
            EditorTool(Icons.Rounded.RestartAlt, stringResource(R.string.controls_editor_reset_selected), "reset_selected", onResetSelected)
            EditorTool(if (selectedVisible) Icons.Rounded.Visibility else Icons.Rounded.VisibilityOff,
                stringResource(R.string.emulation_controls_editor_visible), "visibility", onVisibilityToggle)
            EditorTool(Icons.Rounded.Add, stringResource(R.string.controls_editor_create_combo), "create_combo", onCreate, canCreate)
            EditorTool(Icons.Rounded.ContentCopy, stringResource(R.string.controls_editor_duplicate), "duplicate", onDuplicate, canDuplicate)
            EditorTool(Icons.Rounded.Tune, stringResource(R.string.emulation_controls_editor_size), "adjust", { showAdjust = !showAdjust }, active = showAdjust)
            if (analogMode != null) {
                EditorTool(Icons.Rounded.TouchApp, stringResource(R.string.emulation_controls_editor_touch_area_mode), "analog_mode", onAnalogModeToggle,
                    active = analogMode == TouchAnalogMode.TouchArea)
            }
            EditorTool(Icons.Rounded.GridOn, stringResource(R.string.controls_editor_grid), "grid", onGridToggle, active = showGrid)
            EditorTool(Icons.Rounded.Grid4x4, stringResource(R.string.controls_editor_snap), "snap", onSnapToggle, active = snapToGrid)
            Button(onClick = onDone, modifier = Modifier.testTag("controls_editor_done"), shape = neonShape(16.dp),
                contentPadding = PaddingValues(horizontal = 14.dp, vertical = 10.dp),
                colors = ButtonDefaults.buttonColors(containerColor = Color(0xFF3565FF), contentColor = Color.White)) {
                Text(stringResource(R.string.emulation_controls_editor_done))
            }
        }
        if (showAdjust) {
            Surface(color = Color(0xFF111827).copy(alpha = .92f), shape = neonShape(16.dp)) {
                Column(Modifier.widthIn(max = 420.dp).fillMaxWidth().padding(horizontal = 14.dp, vertical = 10.dp),
                    verticalArrangement = Arrangement.spacedBy(10.dp)) {
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        Text(selectedLabel, Modifier.weight(1f), color = Color.White, maxLines = 1,
                            style = MaterialTheme.typography.labelLarge, fontWeight = FontWeight.SemiBold)
                        IconButton(onClick = { showAdjust = false }, modifier = Modifier.size(28.dp).testTag("controls_editor_adjust_close")) {
                            Icon(Icons.Rounded.Close, stringResource(R.string.emulation_controls_editor_done), tint = Color.White)
                        }
                    }
                    EditorStepper(stringResource(R.string.emulation_controls_editor_percent, selectedScalePercent),
                        onSizeDecrease, onSizeIncrease, minusEnabled = selectedScalePercent > 35, plusEnabled = selectedScalePercent < 250)
                    if (showDimensions) {
                        EditorStepper(stringResource(R.string.emulation_controls_editor_width) + " $touchAreaWidthPercent%",
                            onTouchAreaWidthDecrease, onTouchAreaWidthIncrease, minusEnabled = touchAreaWidthPercent > 50, plusEnabled = touchAreaWidthPercent < 300)
                        EditorStepper(stringResource(R.string.emulation_controls_editor_height) + " $touchAreaHeightPercent%",
                            onTouchAreaHeightDecrease, onTouchAreaHeightIncrease, Modifier.testTag("controls_editor_height_row"),
                            minusEnabled = touchAreaHeightPercent > 50, plusEnabled = touchAreaHeightPercent < 300)
                    }
                }
            }
        } else if (canCombo) {
            Surface(color = Color(0xFF111827).copy(alpha = .82f), shape = neonShape(16.dp)) {
                Row(Modifier.padding(horizontal = 8.dp, vertical = 6.dp), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    OutlinedButton(onClick = onCombo, modifier = Modifier.testTag("controls_editor_combo"), shape = neonShape(14.dp),
                        colors = editorButtonColors()) { Text(stringResource(R.string.controls_editor_combo)) }
                    if (canDelete) {
                        EditorTool(Icons.Rounded.Delete, stringResource(R.string.controls_editor_delete), "delete", { confirmDelete = true })
                    }
                }
            }
        }
    }
    if (confirmDelete && canDelete) {
        AlertDialog(onDismissRequest = { confirmDelete = false }, title = { Text(stringResource(R.string.controls_editor_delete)) },
            text = { Text(selectedLabel) }, confirmButton = {
                TextButton(onClick = { confirmDelete = false; onDelete() }, modifier = Modifier.testTag("controls_editor_confirm_delete_custom")) {
                    Text(stringResource(R.string.controls_editor_delete))
                }
            }, dismissButton = { TextButton(onClick = { confirmDelete = false }) { Text(stringResource(android.R.string.cancel)) } })
    }
}

@Composable
private fun editorButtonColors(active: Boolean = false) = ButtonDefaults.outlinedButtonColors(
    containerColor = if (active) Color(0xFF3565FF).copy(alpha = .78f) else Color.White.copy(alpha = .08f),
    contentColor = Color.White, disabledContentColor = Color.White.copy(alpha = .38f)
)

@Composable
private fun EditorTool(icon: ImageVector, label: String, tag: String, onClick: () -> Unit, enabled: Boolean = true, active: Boolean = false) {
    OutlinedButton(onClick = onClick, enabled = enabled, modifier = Modifier.testTag("controls_editor_$tag"),
        shape = neonShape(16.dp), contentPadding = PaddingValues(horizontal = 14.dp, vertical = 10.dp), colors = editorButtonColors(active)) {
        Icon(icon, contentDescription = label, modifier = Modifier.size(24.dp))
    }
}

@Composable
private fun EditorStepper(value: String, onMinus: () -> Unit, onPlus: () -> Unit, modifier: Modifier = Modifier,
                          minusEnabled: Boolean = true, plusEnabled: Boolean = true) {
    Row(modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
        OutlinedButton(onClick = onMinus, enabled = minusEnabled, shape = neonShape(14.dp), colors = editorButtonColors()) {
            Icon(Icons.Rounded.Remove, contentDescription = "−", modifier = Modifier.size(18.dp))
        }
        Text(value, Modifier.weight(1f).padding(horizontal = 10.dp), color = Color.White,
            style = MaterialTheme.typography.labelLarge, fontWeight = FontWeight.SemiBold, textAlign = androidx.compose.ui.text.style.TextAlign.Center)
        OutlinedButton(onClick = onPlus, enabled = plusEnabled, shape = neonShape(14.dp), colors = editorButtonColors()) {
            Icon(Icons.Rounded.Add, contentDescription = "+", modifier = Modifier.size(18.dp))
        }
    }
}

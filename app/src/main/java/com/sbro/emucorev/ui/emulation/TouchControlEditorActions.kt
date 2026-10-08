@file:OptIn(androidx.compose.foundation.layout.ExperimentalLayoutApi::class)

package com.sbro.emucorev.ui.emulation

import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.FilterChip
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import com.sbro.emucorev.R
import com.sbro.emucorev.ui.theme.neon.neonPillShape

@Composable
internal fun TouchComboEditorDialog(
    actions: List<Pair<String, String>>,
    primary: String,
    secondary: String?,
    primaryEditable: Boolean = true,
    onDismiss: () -> Unit,
    onConfirm: (String, String?) -> Unit
) {
    var selectedPrimary by remember(primary) { mutableStateOf(primary) }
    var selectedSecondary by remember(secondary) { mutableStateOf(secondary) }
    AlertDialog(
        onDismissRequest = onDismiss,
        modifier = Modifier.testTag("controls_editor_combo_dialog"),
        title = { Text(stringResource(R.string.controls_editor_combo)) },
        text = {
            Column(Modifier.heightIn(max = 320.dp).verticalScroll(rememberScrollState()),
                verticalArrangement = Arrangement.spacedBy(8.dp)) {
                Text(stringResource(R.string.controls_editor_combo_description))
                Text(stringResource(R.string.controls_editor_primary_action))
                if (primaryEditable) {
                    ActionChips(actions, selectedPrimary, "primary") {
                        selectedPrimary = it
                        if (selectedSecondary == it) selectedSecondary = null
                    }
                } else {
                    Text(actions.firstOrNull { it.first == selectedPrimary }?.second ?: selectedPrimary)
                }
                Text(stringResource(R.string.controls_editor_secondary_action))
                FilterChip(shape = neonPillShape(), selected = selectedSecondary == null, onClick = { selectedSecondary = null },
                    label = { Text(stringResource(R.string.controls_editor_none)) }, modifier = Modifier.testTag("combo_secondary_none"))
                ActionChips(actions.filterNot { it.first == selectedPrimary }, selectedSecondary, "secondary") {
                    selectedSecondary = it
                }
            }
        },
        confirmButton = {
            TextButton(onClick = { onConfirm(selectedPrimary, selectedSecondary) }, modifier = Modifier.testTag("combo_confirm")) {
                Text(stringResource(R.string.emulation_controls_editor_done))
            }
        },
        dismissButton = {
            TextButton(onClick = onDismiss) { Text(stringResource(android.R.string.cancel)) }
        }
    )
}

@Composable
private fun ActionChips(actions: List<Pair<String, String>>, selected: String?, prefix: String, onSelected: (String) -> Unit) {
    FlowRow(horizontalArrangement = Arrangement.spacedBy(4.dp)) {
        actions.forEach { (id, label) ->
            FilterChip(shape = neonPillShape(), selected = selected == id, onClick = { onSelected(id) }, label = { Text(label) },
                modifier = Modifier.testTag("combo_${prefix}_$id"))
        }
    }
}

package com.sbro.emucorev.ui.setup

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.Inventory2
import androidx.compose.material3.FilledTonalButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import com.sbro.emucorev.R
import com.sbro.emucorev.ui.common.SettingsStyledDialog
import com.sbro.emucorev.ui.theme.neon.neonButtonShape

@Composable
fun ArchivePackageSelectionDialog(
    state: ArchivePackageSelectionUiState?,
    onSelect: (Int) -> Unit,
    onCancel: () -> Unit
) {
    if (state == null) return
    SettingsStyledDialog(
        title = stringResource(R.string.install_choice_title),
        eyebrow = stringResource(R.string.app_name_emucorev),
        icon = Icons.Rounded.Inventory2,
        onDismissRequest = onCancel
    ) {
        Text(
            text = state.message,
            style = MaterialTheme.typography.bodyMedium,
            color = MaterialTheme.colorScheme.onSurfaceVariant
        )
        Column(
            modifier = Modifier
                .fillMaxWidth()
                .verticalScroll(rememberScrollState()),
            verticalArrangement = Arrangement.spacedBy(8.dp)
        ) {
            state.choices.forEach { choice ->
                FilledTonalButton(
                    shape = neonButtonShape(),
                    onClick = { onSelect(choice.id) },
                    modifier = Modifier.fillMaxWidth()
                ) {
                    Column(
                        modifier = Modifier
                            .fillMaxWidth()
                            .padding(vertical = 4.dp),
                        verticalArrangement = Arrangement.spacedBy(3.dp)
                    ) {
                        Text(
                            text = choice.type,
                            style = MaterialTheme.typography.labelMedium.copy(fontWeight = FontWeight.Bold)
                        )
                        Text(
                            text = choice.label,
                            style = MaterialTheme.typography.bodySmall,
                            maxLines = 3
                        )
                    }
                }
            }
        }
        androidx.compose.material3.TextButton(
            shape = neonButtonShape(),
            onClick = onCancel,
            modifier = Modifier.fillMaxWidth()
        ) {
            Text(stringResource(R.string.common_cancel))
        }
    }
}

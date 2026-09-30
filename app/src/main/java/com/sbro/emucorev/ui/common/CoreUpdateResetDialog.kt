package com.sbro.emucorev.ui.common

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import com.sbro.emucorev.R
import com.sbro.emucorev.ui.theme.neon.neonButtonShape
import com.sbro.emucorev.ui.theme.neon.neonShape

/** One-time notice shown after the emulator core changed; keeps every user file intact. */
@Composable
fun CoreUpdateResetDialog(
    onReset: () -> Unit,
    onKeep: () -> Unit
) {
    AlertDialog(
        onDismissRequest = onKeep,
        shape = neonShape(28.dp),
        title = { Text(stringResource(R.string.core_update_reset_title)) },
        text = {
            Column(verticalArrangement = Arrangement.spacedBy(10.dp)) {
                Text(stringResource(R.string.core_update_reset_body))
                Text(
                    text = stringResource(R.string.core_update_reset_kept),
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.primary
                )
            }
        },
        confirmButton = {
            Column(
                modifier = Modifier.fillMaxWidth(),
                verticalArrangement = Arrangement.spacedBy(8.dp)
            ) {
                Button(
                    modifier = Modifier.fillMaxWidth(),
                    shape = neonButtonShape(),
                    onClick = onReset
                ) {
                    Text(stringResource(R.string.core_update_reset_action))
                }
                TextButton(
                    modifier = Modifier.fillMaxWidth(),
                    onClick = onKeep
                ) {
                    Text(stringResource(R.string.core_update_reset_keep))
                }
            }
        }
    )
}

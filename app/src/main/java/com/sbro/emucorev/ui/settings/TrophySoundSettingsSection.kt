package com.sbro.emucorev.ui.settings

import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Button
import androidx.compose.material3.FilterChip
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import com.sbro.emucorev.R
import com.sbro.emucorev.data.TrophySoundMode
import com.sbro.emucorev.data.TrophySoundSettings
import com.sbro.emucorev.ui.common.horizontalBleed
import com.sbro.emucorev.ui.theme.neon.neonButtonShape
import com.sbro.emucorev.ui.theme.neon.neonChipShape

@Composable
internal fun TrophySoundSettingsSection(
    settings: TrophySoundSettings,
    selectionError: Boolean,
    onModeSelected: (TrophySoundMode) -> Unit,
    onChooseAudio: () -> Unit,
    onPreview: () -> Unit,
    modifier: Modifier = Modifier
) {
    Column(
        modifier = modifier.fillMaxWidth().padding(horizontal = 14.dp, vertical = 8.dp),
        verticalArrangement = Arrangement.spacedBy(8.dp)
    ) {
        Text(stringResource(R.string.settings_trophy_sound_title), style = MaterialTheme.typography.titleSmall)
        Text(stringResource(R.string.settings_trophy_sound_description), style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .horizontalBleed(28.dp)
                .horizontalScroll(rememberScrollState())
                .padding(horizontal = 28.dp),
            horizontalArrangement = Arrangement.spacedBy(8.dp)
        ) {
            FilterChip(
                selected = settings.mode == TrophySoundMode.SYSTEM,
                onClick = { onModeSelected(TrophySoundMode.SYSTEM) },
                shape = neonChipShape(),
                label = { Text(stringResource(R.string.settings_trophy_sound_system)) }
            )
            FilterChip(
                selected = settings.mode == TrophySoundMode.OFF,
                onClick = { onModeSelected(TrophySoundMode.OFF) },
                shape = neonChipShape(),
                label = { Text(stringResource(R.string.settings_trophy_sound_off)) }
            )
            FilterChip(
                selected = settings.mode == TrophySoundMode.CUSTOM,
                onClick = {
                    if (settings.customUri == null) onChooseAudio()
                    else onModeSelected(TrophySoundMode.CUSTOM)
                },
                shape = neonChipShape(),
                label = { Text(stringResource(R.string.settings_trophy_sound_custom)) }
            )
        }
        settings.customName?.let { name ->
            Text(
                text = stringResource(R.string.settings_trophy_sound_selected, name),
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
        }
        if (settings.customUnavailable) {
            Text(stringResource(R.string.settings_trophy_sound_unavailable), color = MaterialTheme.colorScheme.error, style = MaterialTheme.typography.bodySmall)
        }
        if (selectionError) {
            Text(stringResource(R.string.settings_trophy_sound_pick_error), color = MaterialTheme.colorScheme.error, style = MaterialTheme.typography.bodySmall)
        }
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            Button(onClick = onChooseAudio, shape = neonButtonShape()) {
                Text(stringResource(R.string.settings_trophy_sound_choose))
            }
            TextButton(onClick = onPreview) {
                Text(stringResource(R.string.settings_trophy_sound_preview))
            }
        }
    }
}
